#include "BodyTexturePremake.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "HangWatch.h"
#include "Memory.h"
#include "engine/facegen/BodyTexturePremakeLogic.h"

namespace obng::plugin {

namespace {

namespace pm = obng::engine::facegen::premake;

constexpr UInt32 kRunSlot = 0x00A36CE8;      // QueuedHead's vtable 0x00A36CE4, slot 1: the task's run on the loader's thread (read)
constexpr UInt32 kRun = 0x00435300;          // __thiscall(task), "QueuedHead::Run()"
constexpr UInt32 kBodyTexture = 0x0052D2C0;  // __thiscall(race, base out, own out, npc, part), RET 0x10 (read)
constexpr UInt32 kTaskNpc = 0x20;            // the task's NPC (read: 0x00435321, the `this` of 0x00523220)
constexpr UInt32 kFormType = 0x4;            // a form's type byte
constexpr UInt32 kRace = 0xE8;               // an NPC's race (read: 0x0047ADED)
constexpr UInt32 kTexturing = 0x00B10D3C;    // bFaceGenTexturing, a byte (read: 0x0052D2C0)
constexpr UInt32 kLoader = 0x00B33A10;       // the manager whose thread (+0x30) 0x00442890 loads on without a pause (read: 0x00442960)
constexpr UInt32 kLoaderThread = 0x30;
constexpr UInt32 kTextureMap = 0x00B35300;   // the texture map; its find in slot 1: __thiscall(map, name, 0) (read: 0x004428C3, 0x004A1D80)
constexpr UInt32 kFormId = 0xC;              // a form's id (read: 0x0052D2C0's sprintf)
constexpr UInt32 kActorFlags = 0x28;         // an NPC's flags, bit 0 female (read: 0x00519D20)
constexpr UInt32 kSkinCall = 0x0047ADFF;     // the skin parts' call of 0x0052D2C0 on the main thread (read; the only call in the EXE)

using RunFn = void(__thiscall*)(void* task);
using BodyTextureFn = bool(__thiscall*)(void* race, UInt32* base, UInt32* own, void* npc, int part);
using DeleteFn = void(__thiscall*)(void* object, UInt32 flags);
using FindFn = UInt32(__thiscall*)(void* map, const char* name, UInt32 zero);

LogFn g_log = nullptr;
bool g_on = false;
double g_ticksPerMs = 0.0;
CRITICAL_SECTION g_statsLock;
CRITICAL_SECTION g_bodyLock;  // one synthesis from a body's .egt at a time across the two threads (BodyTexturePremakeLogic.h)
pm::Totals g_totals;
volatile LONG g_threadsLogged = 0;

UInt32 At(UInt32 address) {
	return *reinterpret_cast<const UInt32*>(static_cast<std::uintptr_t>(address));
}

UInt8 ByteAt(UInt32 address) {
	return *reinterpret_cast<const UInt8*>(static_cast<std::uintptr_t>(address));
}

void* Pointer(UInt32 address) {
	return reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
}

// Lets go of a counted object the way the engine's smart pointer does: the count at +4, the deleting destructor in slot 0.
void Release(UInt32 object) {
	if (object != 0 && InterlockedDecrement(reinterpret_cast<LONG*>(static_cast<std::uintptr_t>(object + 4))) == 0) {
		reinterpret_cast<DeleteFn>(static_cast<std::uintptr_t>(At(At(object))))(Pointer(object), 1);
	}
}

// Whether the NPC's own texture for the part is in the texture map; keyKnown says whether the part has one at all.
bool InMap(UInt32 npc, int part, bool& keyKnown) {
	char key[pm::kKeySize];
	keyKnown = pm::KeyOf(part, (ByteAt(npc + kActorFlags) & 1) != 0, At(npc + kFormId), key);
	const UInt32 map = At(kTextureMap);
	if (!keyKnown || map == 0) {
		return false;
	}
	return reinterpret_cast<FindFn>(static_cast<std::uintptr_t>(At(At(map) + 4)))(Pointer(map), key, 0) != 0;
}

void Count(pm::Skip skip, std::uint64_t ticks, unsigned textures) {
	EnterCriticalSection(&g_statsLock);
	pm::Count(g_totals, skip, ticks, textures);
	LeaveCriticalSection(&g_statsLock);
}

void Premake(UInt32 npc) {
	const UInt8 type = npc != 0 ? ByteAt(npc + kFormType) : 0;
	const UInt32 race = type == pm::kNpcFormType ? At(npc + kRace) : 0;
	const UInt32 loader = At(kLoader);
	if (InterlockedExchange(&g_threadsLogged, 1) == 0) {
		g_log("Route: body texture premake - the first queued head runs on thread %lu; the thread the texture load takes for the loader's is %lu",
		      static_cast<unsigned long>(GetCurrentThreadId()), static_cast<unsigned long>(loader != 0 ? At(loader + kLoaderThread) : 0));
	}
	bool made = false;
	if (race != 0) {
		bool known = false;
		made = InMap(npc, 2, known) && InMap(npc, 3, known);  // the upper body's and the one of every other part
	}
	pm::Facts facts{npc != 0, type, race != 0, ByteAt(kTexturing) != 0, loader != 0 && GetCurrentThreadId() == At(loader + kLoaderThread), made, false};
	// Never a wait here: the main thread may hold the lock inside 0x0052D2C0 while it waits for this very task to end.
	const bool locked = pm::Decide(facts) == pm::Skip::None && TryEnterCriticalSection(&g_bodyLock) != 0;
	facts.mainThreadInside = !locked;
	const pm::Skip skip = pm::Decide(facts);
	if (skip != pm::Skip::None) {
		Count(skip, 0, 0);
		return;
	}
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	unsigned textures = 0;
	for (const int part : pm::kParts) {
		UInt32 base = 0, own = 0;
		reinterpret_cast<BodyTextureFn>(static_cast<std::uintptr_t>(kBodyTexture))(Pointer(race), &base, &own, Pointer(npc), part);
		textures += own != 0 ? 1 : 0;
		Release(base);  // the texture map keeps both
		Release(own);
	}
	LeaveCriticalSection(&g_bodyLock);
	QueryPerformanceCounter(&t1);
	Count(pm::Skip::None, static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart), textures);
}

// In place of the skin parts' call on the main thread: the same call under OBNG's lock, so it never runs beside a
// premake's on the loader's thread (BodyTexturePremakeLogic.h). It may wait for one NPC's premake; the premake never
// waits for it.
bool __fastcall SkinTexture(void* race, void*, UInt32* base, UInt32* own, void* npc, int part) {
	EnterCriticalSection(&g_bodyLock);
	const bool result = reinterpret_cast<BodyTextureFn>(static_cast<std::uintptr_t>(kBodyTexture))(race, base, own, npc, part);
	LeaveCriticalSection(&g_bodyLock);
	return result;
}

// In place of the queued head's run: the run (the head, under FaceGen's data lock 0x00B39C80), then the same NPC's
// body textures. The NPC is read before the run; a form outlives the task.
void __fastcall Run(void* task) {
	const UInt32 npc = At(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(task)) + kTaskNpc);
	reinterpret_cast<RunFn>(static_cast<std::uintptr_t>(kRun))(task);
	Premake(npc);
}

}  // namespace

void InstallBodyTexturePremake(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (!pm::OnOf(static_cast<int>(GetPrivateProfileIntA("Route", "BodyTexturePremake", pm::Shipped(), iniPath)))) {
		log("Route: body texture premake - off in OBNG.ini; an NPC's body textures are made at its task's end on the main thread, as the game does it");
		return;
	}
	g_log = log;
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	g_ticksPerMs = static_cast<double>(frequency.QuadPart) / 1000.0;
	InitializeCriticalSection(&g_statsLock);
	InitializeCriticalSection(&g_bodyLock);
	HangWatchLock("OBNG's body texture", &g_bodyLock);
	const UInt32 expected = kRun;
	if (!Verify(kRunSlot, reinterpret_cast<const UInt8*>(&expected), sizeof(expected))) {
		log("Route: body texture premake - the queued head's run slot at %08X is not the game's own; not routed", kRunSlot);
		return;
	}
	// The main thread's call first: without the lock on it a premake could run beside it.
	UInt8 call[5] = {0xE8}, locked[5] = {0xE8};
	const UInt32 toOriginal = kBodyTexture - (kSkinCall + 5);
	const UInt32 toLocked = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&SkinTexture)) - (kSkinCall + 5);
	std::memcpy(call + 1, &toOriginal, 4);
	std::memcpy(locked + 1, &toLocked, 4);
	if (!Verify(kSkinCall, call, sizeof(call)) || !SafeWrite(kSkinCall, locked, sizeof(locked))) {
		log("Route: body texture premake - the skin parts' call at %08X is not the game's own or could not be written; not routed", kSkinCall);
		return;
	}
	const UInt32 hook = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&Run));
	if (!SafeWrite(kRunSlot, &hook, sizeof(hook))) {
		log("Route: body texture premake - the run slot at %08X could not be written; not routed", kRunSlot);
		return;
	}
	g_on = true;
	log("Route: body texture premake - an NPC's body textures are made on the loader's thread, after its queued head");
}

void LogBodyTexturePremakeSummary() {
	if (!g_on) {
		return;
	}
	EnterCriticalSection(&g_statsLock);
	const pm::Totals totals = g_totals;
	LeaveCriticalSection(&g_statsLock);
	const auto skipped = [&totals](pm::Skip skip) { return totals.skipped[static_cast<std::size_t>(skip)]; };
	g_log("Route: body texture premake - since the start %llu NPCs' textures asked for on the loader's thread (%.1f ms in all, the longest %.1f ms, "
	      "%llu own textures came back); skipped: no NPC %llu, not an NPC %llu, no race %llu, texturing off %llu, not the loader's thread %llu, "
	      "already in the map %llu, the main thread inside the body texture call %llu",
	      totals.made, static_cast<double>(totals.ticks) / g_ticksPerMs, static_cast<double>(totals.longest) / g_ticksPerMs, totals.withTexture,
	      skipped(pm::Skip::NoNpc), skipped(pm::Skip::NotAnNpc), skipped(pm::Skip::NoRace), skipped(pm::Skip::TexturingOff),
	      skipped(pm::Skip::NotTheLoaderThread), skipped(pm::Skip::AlreadyMade), skipped(pm::Skip::MainThreadInside));
}

}  // namespace obng::plugin
