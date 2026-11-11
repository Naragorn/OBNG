#include "ActorCensus.h"

#include <windows.h>

#include <cstring>

#include "ActorCensusLogic.h"
#include "Memory.h"

namespace obng::plugin {

namespace {

constexpr const char* kName = "Actor::Update (census)";
constexpr UInt32 kAddress = 0x00603CA0;
constexpr UInt8 kBytes[] = {0x6A, 0xFF, 0x68, 0x60, 0x2C, 0x9C, 0x00};  // push -1; push 009C2C60h (read)
constexpr UInt32 kPlayerPointer = 0x00B333C4;  // (read 0x00603CA0's compares; xOBSE g_thePlayer, OBVR kPlayerPointer)
// TESForm and TESObjectREFR as xOBSE's GameForms.h / GameObjects.h have them
// and OBVR reads them (GameAddresses.h kRefPositionOffset 0x2C, the node at
// +0x3C): the type byte +0x04, the form id +0x0C; the reference's base form
// +0x1C, position +0x2C, scene node +0x3C. The node's flags word +0x18, bit
// 0 the hidden bit (SceneUpdate.h kFlagsOffset; knowledge/render-thread-
// outside-the-pass.md has bit 0 as APP_CULLED).
constexpr UInt32 kFormType = 0x04;
constexpr UInt32 kFormId = 0x0C;
constexpr UInt32 kRefBase = 0x1C;
constexpr UInt32 kRefPosition = 0x2C;
constexpr UInt32 kRefNode = 0x3C;
constexpr UInt32 kNodeFlags = 0x18;

census::Sample g_sample;  // written by the game's thread while collecting, read by the summary's when ready
volatile LONG g_state = 0;  // 0 idle, 1 wanted or collecting, 2 ready: the hand-over between the two threads
void* g_original = nullptr;
bool g_installed = false;
LogFn g_log = nullptr;

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }

void __cdecl Note(UInt32 actor) {
	if (g_state != 1) {
		return;
	}
	if (!g_sample.Visit(actor)) {
		if (g_sample.Now() == census::State::Ready) {
			InterlockedExchange(&g_state, 2);
		}
		return;
	}
	census::Seen s;
	s.actor = actor;
	s.refId = Dword(actor + kFormId);
	const UInt32 base = Dword(actor + kRefBase);
	if (base != 0) {
		s.baseId = Dword(base + kFormId);
		s.baseType = *reinterpret_cast<const UInt8*>(base + kFormType);
	}
	const UInt32 node = Dword(actor + kRefNode);
	s.hasNode = node != 0;
	s.culled = node != 0 && (*reinterpret_cast<const UInt16*>(node + kNodeFlags) & 1u) != 0;
	const UInt32 player = Dword(kPlayerPointer);
	if (player != 0) {
		float here[3];
		float there[3];
		std::memcpy(here, reinterpret_cast<const void*>(actor + kRefPosition), sizeof(here));
		std::memcpy(there, reinterpret_cast<const void*>(player + kRefPosition), sizeof(there));
		s.distance = census::Distance(here, there);
	}
	g_sample.Add(s);
}

}  // namespace

void InstallActorCensus(const ShadowSettings& settings, LogFn log) {
	if (g_installed || !settings.actorCensus) {
		return;
	}
	g_log = log;
	auto* hop = static_cast<UInt8*>(AllocExecutable(8));
	auto* thunk = static_cast<UInt8*>(AllocExecutable(census::kNoteThunkSize));
	if (hop == nullptr || thunk == nullptr) {
		log("Census: no executable memory; off");
		return;
	}
	// The entry goes to a hop that jumps through g_original until the thunk
	// (which needs the way into the original) is built - as TimedCalls.cpp.
	hop[0] = 0xFF;
	hop[1] = 0x25;
	const UInt32 pointerAt = reinterpret_cast<UInt32>(&g_original);
	std::memcpy(hop + 2, &pointerAt, 4);
	if (!InstallEntryDetour(kName, kAddress, kBytes, sizeof(kBytes), hop, &g_original, log)) {
		return;
	}
	if (census::BuildNoteThunk(thunk, census::kNoteThunkSize, reinterpret_cast<UInt32>(thunk), reinterpret_cast<UInt32>(&Note),
	                           reinterpret_cast<UInt32>(g_original)) == 0) {
		return;
	}
	UInt8 jump[5];
	jump[0] = 0xE9;
	const UInt32 rel = reinterpret_cast<UInt32>(thunk) - (reinterpret_cast<UInt32>(hop) + 5);
	std::memcpy(jump + 1, &rel, 4);
	std::memcpy(hop, jump, 5);
	FlushInstructionCache(GetCurrentProcess(), hop, 8);
	g_installed = true;
}

void LogActorCensusSummary() {
	if (!g_installed) {
		return;
	}
	if (g_state == 2) {
		char line[900];
		census::FormatCounts(census::Count(g_sample), line, sizeof(line));
		g_log("%s", line);
		for (std::size_t at = 0; at < g_sample.Size();) {
			const std::size_t next = census::FormatActors(g_sample, at, line, sizeof(line));
			if (next == at) {
				break;
			}
			g_log("%s", line);
			at = next;
		}
		g_sample.Taken();
		InterlockedExchange(&g_state, 0);
	}
	if (g_state == 0) {
		g_sample.Ask();
		InterlockedExchange(&g_state, 1);
	}
}

}  // namespace obng::plugin
