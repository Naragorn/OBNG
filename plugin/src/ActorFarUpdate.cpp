#include "ActorFarUpdate.h"

#include <windows.h>

#include <cstring>

#include "engine/scene/ActorFarRateLogic.h"
#include "engine/scene/ActorFarUpdateLogic.h"

namespace obng::plugin {

namespace {

namespace far_rate = obng::engine::scene::actor_far_rate;
namespace far_update = obng::engine::scene::actor_far_update;

constexpr const char* kName = "Actor::Update (far actors)";
constexpr UInt32 kAddress = 0x00603CA0;  // __thiscall, one float, RET 4 at 0x00605759 (read)
constexpr UInt8 kBytes[] = {0x6A, 0xFF, 0x68, 0x60, 0x2C, 0x9C, 0x00};  // push -1; push 009C2C60h (read)
constexpr UInt32 kPlayerPointer = 0x00B333C4;  // (read 0x00603CA0's compares; xOBSE g_thePlayer, OBVR kPlayerPointer)
constexpr UInt32 kFrameSeconds = 0x00B33E9C;   // frameSeconds as Actor::Update reads it (read: seven places in 0x00603CA0; knowledge/actor-shadows.md)
constexpr UInt32 kClockMs = 0x00B33EA0;        // TimeInfo 0x00B33E90 +0x10 clockMs: moves once a frame (knowledge/frame-clock.md; xOBSE g_timeInfo)
constexpr UInt32 kRefPosition = 0x2C;          // (OBVR GameAddresses.h kRefPositionOffset)
constexpr UInt32 kActorProcess = 0x58;         // the process (read 0x00659A00: [this+0x58] -> vfunc +0x08 is the level)
constexpr UInt32 kProcessLevelSlot = 0x08;

using UpdateFn = void(__thiscall*)(void* self, float time);
using LevelFn = int(__thiscall*)(void* self);

void* g_original = nullptr;
bool g_installed = false;
LogFn g_log = nullptr;
std::uint32_t g_every = 1;
float g_distance = 6000.0f;
bool g_alternate = false;
DWORD g_thread = 0;                // the thread of the first call: the only one whose calls are left out
volatile LONG g_onNow = 1;         // with the alternation: whether this summary window uses the lower rate
volatile LONG64 g_farCalls = 0;    // calls of far actors
volatile LONG64 g_skipped = 0;     // of them left out
volatile LONG64 g_summed = 0;      // calls run with summed time
far_update::Clock g_clock;
far_update::Table g_table;

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }

void Original(void* self, float time) { reinterpret_cast<UpdateFn>(g_original)(self, time); }

// Whether the actor stands beyond the distance from the player. A position
// that is not a number compares false: near.
bool IsFar(UInt32 actor, UInt32 player) {
	float here[3];
	float there[3];
	std::memcpy(here, reinterpret_cast<const void*>(actor + kRefPosition), sizeof(here));
	std::memcpy(there, reinterpret_cast<const void*>(player + kRefPosition), sizeof(there));
	const float x = here[0] - there[0];
	const float y = here[1] - there[1];
	const float z = here[2] - there[2];
	return x * x + y * y + z * z > g_distance * g_distance;
}

// The entry's replacement: __fastcall with an unused second register takes a
// __thiscall's this and cleans the one stack argument as the original does.
void __fastcall Update(void* self, void*, float time) {
	const UInt32 actor = reinterpret_cast<UInt32>(self);
	const UInt32 player = Dword(kPlayerPointer);
	const DWORD thread = GetCurrentThreadId();
	if (g_thread == 0) {
		g_thread = thread;
	}
	auto* frameSeconds = reinterpret_cast<float*>(kFrameSeconds);
	if (thread != g_thread || player == 0) {
		Original(self, time);
		return;
	}
	g_clock.See(Dword(kClockMs), *frameSeconds);
	if (actor == player) {
		Original(self, time);
		return;
	}
	// The high loop's call only: time 0 and the high process (level 0). The
	// lower levels have their own intervals (0x00674200, 0x00673E90).
	const UInt32 process = Dword(actor + kActorProcess);
	if (time != 0.0f || process == 0 ||
	    reinterpret_cast<LevelFn>(Dword(Dword(process) + kProcessLevelSlot))(reinterpret_cast<void*>(process)) != 0) {
		Original(self, time);
		return;
	}
	std::uint32_t every = 1;
	if (IsFar(actor, player)) {
		InterlockedIncrement64(&g_farCalls);
		every = g_onNow != 0 ? g_every : 1;
	}
	bool isNew = false;
	far_update::Entry& entry = g_table.Find(actor, g_clock.frame, isNew);
	const far_update::Step step = far_update::Decide(entry, isNew, every, g_clock, *frameSeconds);
	if (!step.runs) {
		InterlockedIncrement64(&g_skipped);
		return;
	}
	if (!step.summed) {
		Original(self, time);
		return;
	}
	InterlockedIncrement64(&g_summed);
	const float frame = *frameSeconds;
	*frameSeconds = step.seconds;
	Original(self, time);
	*frameSeconds = frame;
}

}  // namespace

void InstallActorFarUpdate(const char* iniPath, LogFn log) {
	if (g_installed || GetPrivateProfileIntA("Route", "ActorFarWhole", 0, iniPath) == 0) {
		return;
	}
	const UINT every = GetPrivateProfileIntA("Route", "ActorFarEvery", 1, iniPath);
	if (every <= 1) {
		log("Route %s: ActorFarWhole=1 with ActorFarEvery=1; off", kName);
		return;
	}
	g_log = log;
	g_every = every > 64 ? 64 : every;
	g_distance = static_cast<float>(GetPrivateProfileIntA("Route", "ActorFarDistance", 6000, iniPath));
	g_alternate = GetPrivateProfileIntA("Route", "ActorFarAlternate", 0, iniPath) != 0;
	if (!InstallEntryDetour(kName, kAddress, kBytes, sizeof(kBytes), reinterpret_cast<const void*>(&Update), &g_original, log, DetourRole::Route)) {
		return;
	}
	g_installed = true;
	log("Route %s: the whole update of an actor beyond %.0f units on every %u. call, with the summed time%s", kName,
	    static_cast<double>(g_distance), g_every, g_alternate ? "; alternating: only in every second summary window" : "");
}

void LogActorFarUpdateSummary() {
	if (!g_installed) {
		return;
	}
	const LONG64 farCalls = InterlockedExchange64(&g_farCalls, 0);
	const LONG64 skipped = InterlockedExchange64(&g_skipped, 0);
	const LONG64 summed = InterlockedExchange64(&g_summed, 0);
	const bool wasOn = g_onNow != 0;
	if (g_alternate) {
		InterlockedExchange(&g_onNow, wasOn ? 0 : 1);
	}
	g_log("Route: far actors' whole update (every %u. call beyond %.0f units, %s in this window) - %lld calls of far actors, %lld left out, %lld run with summed time",
	      g_every, static_cast<double>(g_distance), wasOn ? "on" : "off", farCalls, skipped, summed);
}

}  // namespace obng::plugin
