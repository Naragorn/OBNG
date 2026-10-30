// The range-walk shadow: OBNG's first subsystem replacement checked in the
// game (docs/subsystems/shadow-light-range-walk.md, "How the in-game gate
// will compare the two"). Two entry detours:
//
//   0x007D59E0 ShadowSceneLight::CollectGeometryInRange (proposed name) -
//     the outermost call per thread reads the light, runs the original with
//     its AddGeometry calls recorded, then runs BatchedWalk over the same
//     scene through ports that read the game's objects and call the engine's
//     own helpers but record AddGeometry, and compares. The walk's own
//     recursive calls (0x007D5A74, 0x007D5ABD) come back through this entry
//     and pass straight through.
//   0x007D5790 ShadowSceneLight::AddGeometry (proposed name) - records the
//     geometry and the light's +0x1B0 while a compared walk of that light
//     runs on this thread, then calls the original. Its other caller
//     0x007C6100 is not recorded.
//
// The game always continues with the original's result. The flow and every
// decision are in ShadowRangeWalkLogic.h; this file supplies the 32-bit
// engine shape: the volatile reads, the thiscall calls, the square root.

#include "ShadowRangeWalk.h"

#include <windows.h>

#include <cstddef>
#include <cstdio>

#include "ShadowRangeWalkLogic.h"
#include "ShadowRecorder.h"
#include "engine/render/ShadowSceneLight_IsInRange.h"

namespace obng::plugin {
namespace {

namespace rw = range_walk;
namespace walk_engine = engine::render::shadow_light_range_walk::engine;
namespace in_range_engine = engine::render::shadow_scene_light_is_in_range::engine;

// The logic's offsets against the IsInRange rebuild's views (both read in
// re/functions/0x007D2EA0.md; the views carry the instruction addresses).
static_assert(rw::kSourcePositionOffset == offsetof(in_range_engine::NiLightView, worldTranslate), "source +0x88");
static_assert(rw::kSourceRangeOffset == offsetof(in_range_engine::NiLightView, range), "source +0xF8");
static_assert(walk_engine::kPlaneSourceOffset == offsetof(in_range_engine::ShadowSceneLightView, planeSource), "+0x14C");
static_assert(walk_engine::kPlanesOffset == offsetof(in_range_engine::ShadowSceneLightView, planes), "+0x150");
static_assert(walk_engine::kActivePlanesOffset == offsetof(in_range_engine::ShadowSceneLightView, activePlanes), "+0x1B0");
static_assert(rw::kPlaneStride == sizeof(in_range_engine::NiPlane), "16-byte planes");
static_assert(walk_engine::kWorldBoundOffset == offsetof(in_range_engine::NiAVObjectView, worldBound), "+0x20");

// The entries (re/export/range-callers/*/asm.s and bytes.bin): whole
// instructions, nothing relative. The walk's first two instructions are 8
// bytes; no jump in its body targets 0x007D59E1..0x007D59E7 (its asm.s), its
// callers enter at 0x007D59E0 (meta.json). AddGeometry's first two are 7
// bytes (its SEH frame setup), no jump in its body targets inside them.
constexpr UInt32 kWalk = walk_engine::kCollectGeometryInRangeAddress;
constexpr UInt8 kWalkBytes[] = {0x51, 0x83, 0x3D, 0x48, 0x2F, 0xB4, 0x00, 0x00};  // push ecx; cmp dword [0x00B42F48],0
constexpr UInt32 kAddGeometry = walk_engine::kAddGeometryAddress;
constexpr UInt8 kAddGeometryBytes[] = {0x6A, 0xFF, 0x68, 0xB8, 0xEB, 0x9C, 0x00};  // push -1; push 0x009CEBB8

// __fastcall with a dummy edx mirrors __thiscall: ECX is this, the one stack
// argument cleaned by the callee (RET 4 in both originals, read). The walk's
// result is AL with the upper bits undefined (C1); EAX is passed on whole.
using WalkFn = UInt32(__fastcall*)(UInt32 light, void* edx, UInt32 object);
using AddGeometryFn = UInt32(__fastcall*)(UInt32 light, void* edx, UInt32 geometry);
// A thiscall without a stack argument: the virtual slots +0x04/+0x08/+0x10
// (called with ECX = the object and nothing pushed, 0x007D5A20, 0x007D5A35,
// 0x007D5AEA) and the helpers 0x007C59E0 / 0x0049FFC0 (plain RET, read in
// their asm.s).
using ThisFn = UInt32(__fastcall*)(UInt32 self, void* edx);

WalkFn g_walk = nullptr;
AddGeometryFn g_addGeometry = nullptr;
bool g_on = false;
LogFn g_log = nullptr;

SRWLOCK g_statsLock = SRWLOCK_INIT;
rw::WalkStats g_stats;

// Per thread: the depth, the recording and the buffers, in an explicit TLS
// slot (TlsAlloc) rather than thread_local, so nothing rests on the loader
// giving a LoadLibrary'd DLL implicit TLS. Allocated by a thread's first walk.
struct ThreadSlot {
	rw::ThreadWalkState state;
	rw::WalkBuffers buffers;
};
DWORD g_tls = TLS_OUT_OF_INDEXES;

// TlsGetValue sets the thread's last error on success; the game's is kept.
ThreadSlot* ExistingSlot() {
	const DWORD lastError = GetLastError();
	auto* slot = static_cast<ThreadSlot*>(TlsGetValue(g_tls));
	SetLastError(lastError);
	return slot;
}

ThreadSlot& Slot() {
	ThreadSlot* slot = ExistingSlot();
	if (slot == nullptr) {
		const DWORD lastError = GetLastError();
		slot = new ThreadSlot();
		TlsSetValue(g_tls, slot);
		SetLastError(lastError);
	}
	return *slot;
}

template <class T>
T Read(UInt32 address) {
	return *reinterpret_cast<volatile const T*>(address);
}

// The memory port of ShadowRangeWalkLogic.h over the game's own memory.
struct GameMemory {
	std::uint8_t U8(std::uint32_t address) { return Read<std::uint8_t>(address); }
	std::uint16_t U16(std::uint32_t address) { return Read<std::uint16_t>(address); }
	std::uint32_t U32(std::uint32_t address) { return Read<std::uint32_t>(address); }
	float F32(std::uint32_t address) { return Read<float>(address); }
	std::uint32_t CallVirtual(std::uint32_t object, std::uint32_t slot) {
		const UInt32 vtable = Read<UInt32>(object);
		return reinterpret_cast<ThisFn>(Read<UInt32>(vtable + slot))(object, nullptr);
	}
	std::uint32_t CallThis(std::uint32_t function, std::uint32_t self) {
		return reinterpret_cast<ThisFn>(function)(self, nullptr);
	}
};

// The square root IsInRange's distance path calls: 0x00982C30 takes its
// argument in ST0 and returns in ST0 (read, C14 of re/functions/0x007D2EA0.md);
// a cdecl function returns a double in ST0 as well. So: load the float, tail
// jump; its RET comes back to our caller with the result where it expects it.
// The game's own routine runs, under the game's x87 control word, so the
// comparison does not rest on a model of its rounding.
__declspec(naked) double __cdecl GameSquareRoot(float /*s*/) {
	__asm {
		fld dword ptr [esp + 4]
		mov eax, 0x00982C30
		jmp eax
	}
}

const char* ModeName(engine::render::shadow_light_range_walk::SourceMode mode) {
	switch (mode) {
	case engine::render::shadow_light_range_walk::SourceMode::NoSource: return "no source";
	case engine::render::shadow_light_range_walk::SourceMode::Directional: return "directional";
	case engine::render::shadow_light_range_walk::SourceMode::Planes: return "planes";
	case engine::render::shadow_light_range_walk::SourceMode::Distance: return "distance";
	}
	return "?";
}

void Report(const rw::WalkReport& report) {
	if (report.action != rw::EntryAction::Compare) {
		RecordSkipped(Shadowed::RangeWalk, rw::SkipReason(report.action));
		return;
	}
	AcquireSRWLockExclusive(&g_statsLock);
	g_stats.Add(report);
	ReleaseSRWLockExclusive(&g_statsLock);
	const bool mismatch = report.comparison.Mismatch();
	char detail[400] = {};
	if (mismatch) {
		const rw::WalkBuffers& buffers = Slot().buffers;  // this thread's: Report runs inside its walk
		rw::DescribeComparison(report.comparison, buffers.original, buffers.rebuild, detail, sizeof(detail));
	}
	char inputs[160];
	std::snprintf(inputs, sizeof(inputs), "light %08X root %08X source %s mask %08X thread %lu", report.light,
	              report.object, ModeName(report.mode), report.maskBefore, GetCurrentThreadId());
	RecordCall(Shadowed::RangeWalk, mismatch, detail, inputs);
	SummaryIfDue(GetTickCount(), nullptr, false);
}

UInt32 __fastcall ShadowAddGeometry(UInt32 light, void* /*edx*/, UInt32 geometry) {
	// A thread without a slot has never run a walk, so nothing to record.
	if (ThreadSlot* slot = ExistingSlot()) {
		rw::OnAddGeometry(slot->state, light, geometry,
		                  [light] { return Read<UInt32>(light + walk_engine::kActivePlanesOffset); });
	}
	return g_addGeometry(light, nullptr, geometry);
}

UInt32 __fastcall ShadowWalk(UInt32 light, void* /*edx*/, UInt32 object) {
	ThreadSlot& slot = Slot();
	GameMemory memory;
	return rw::ShadowWalk(memory, slot.state, slot.buffers, light, object,
	                      [](std::uint32_t l, std::uint32_t o) { return static_cast<std::uint32_t>(g_walk(l, nullptr, o)); },
	                      &GameSquareRoot, &Report);
}

}  // namespace

UInt32 InstallShadowRangeWalk(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.rangeWalk) {
		return 0;
	}
	s_done = true;
	g_log = log;
	g_tls = TlsAlloc();
	if (g_tls == TLS_OUT_OF_INDEXES) {
		log("Shadow %s: no TLS slot; not shadowed", ShadowName(Shadowed::RangeWalk));
		return 0;
	}
	// AddGeometry first: without its recording every compared walk would
	// show the original as empty.
	if (!InstallEntryDetour("ShadowSceneLight::AddGeometry (recording)", kAddGeometry, kAddGeometryBytes,
	                        sizeof(kAddGeometryBytes), reinterpret_cast<const void*>(&ShadowAddGeometry),
	                        reinterpret_cast<void**>(&g_addGeometry), log)) {
		log("Shadow %s: not shadowed - its AddGeometry recording could not be put", ShadowName(Shadowed::RangeWalk));
		return 0;
	}
	if (!InstallEntryDetour(ShadowName(Shadowed::RangeWalk), kWalk, kWalkBytes, sizeof(kWalkBytes),
	                        reinterpret_cast<const void*>(&ShadowWalk), reinterpret_cast<void**>(&g_walk), log)) {
		return 1;
	}
	g_on = true;
	return 2;
}

void LogRangeWalkSummary() {
	if (!g_on || g_log == nullptr) {
		return;
	}
	AcquireSRWLockExclusive(&g_statsLock);
	const rw::WalkStats stats = g_stats;
	g_stats.Reset();
	ReleaseSRWLockExclusive(&g_statsLock);
	char line[300];
	rw::FormatWalkStats(stats, line, sizeof(line));
	g_log("%s", line);
}

}  // namespace obng::plugin
