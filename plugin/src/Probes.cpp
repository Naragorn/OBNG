// The counting detours and the memory sample. Each detour counts the call
// and its return site, then calls the original with the same arguments; the
// game sees no difference. The counts are kept with interlocked operations,
// because NiBound::WhichSide runs for every plane of every object and must
// not take a lock. The memory sample is taken when the summary is due.

#include "Probes.h"

#include <windows.h>

#include <intrin.h>

#include <cstdio>

#include "ProbeLogic.h"
#include "RangeWalkGame.h"
#include "ShadowRecorder.h"

namespace obng::plugin {
namespace {

enum class Probed : int {
	SetupTransformations = 0,  // 0x007C9230, shader vtable slot 13 (re/functions/0x007C9230.md)
	Cull,                      // 0x0070DFB0, NiCullingProcess::Cull (re/functions/0x0070DFB0.md)
	IsInRange,                 // 0x007D2EA0, ShadowSceneLight::IsInRange (re/functions/0x007D2EA0.md)
	CopyVertexStreamElements,  // 0x0076E610 (re/functions/0x0076E610.md)
	WhichSide,                 // 0x0047DA70, NiBound::WhichSide (re/functions/0x0047DA70.md)
	Count,
};

const char* const kNames[] = {"SetupTransformations", "NiCullingProcess::Cull", "ShadowSceneLight::IsInRange",
                              "CopyVertexStreamElements", "NiBound::WhichSide"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(Probed::Count), "one name per probed function");

// The entries (asm.s of the exports): whole instructions, nothing relative.
constexpr UInt32 kSetup = 0x007C9230;
constexpr UInt8 kSetupBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-16
constexpr UInt32 kCull = 0x0070DFB0;
constexpr UInt8 kCullBytes[] = {0x51, 0x55, 0x8B, 0xE9, 0x8B, 0x85, 0x8C, 0x00, 0x00, 0x00};  // push ecx; push ebp; mov ebp,ecx; mov eax,[ebp+8Ch]
constexpr UInt32 kInRange = 0x007D2EA0;
constexpr UInt8 kInRangeBytes[] = {0x83, 0xEC, 0x1C, 0x53, 0x56};  // sub esp,1Ch; push ebx; push esi
constexpr UInt32 kCopy = 0x0076E610;
constexpr UInt8 kCopyBytes[] = {0x83, 0xEC, 0x0C, 0x53, 0x55};  // sub esp,0Ch; push ebx; push ebp
constexpr UInt32 kWhichSide = 0x0047DA70;
constexpr UInt8 kWhichSideBytes[] = {0x8B, 0x44, 0x24, 0x04, 0xD9, 0x40, 0x04};  // mov eax,[esp+4]; fld dword [eax+4]

// __fastcall with a dummy edx mirrors __thiscall: ECX is this, the rest on
// the stack, the callee cleans (RET N as the original does).
using SetupFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32);
using CullFn = void(__fastcall*)(void* self, void* edx, UInt32 object);
using InRangeFn = UInt8(__fastcall*)(void* self, void* edx, UInt32 object, UInt32 light);
using CopyFn = UInt32(__cdecl*)(const void* desc);
using WhichSideFn = int(__fastcall*)(const void* sphere, void* edx, const void* plane);

SetupFn g_setup = nullptr;
CullFn g_cull = nullptr;
InRangeFn g_inRange = nullptr;
CopyFn g_copy = nullptr;
WhichSideFn g_whichSide = nullptr;

// The interlocked counterpart of probe::CallCounts: a slot is claimed with a
// compare-exchange on its address, the counts are incremented interlocked.
struct AtomicCounts {
	volatile LONG64 calls = 0;
	volatile LONG addresses[probe::ReturnSites::kSites] = {};
	volatile LONG64 counts[probe::ReturnSites::kSites] = {};
	volatile LONG64 others = 0;

	void Add(UInt32 returnAddress) {
		InterlockedIncrement64(&calls);
		for (unsigned i = 0; i < probe::ReturnSites::kSites; ++i) {
			const LONG seen = InterlockedCompareExchange(&addresses[i], static_cast<LONG>(returnAddress), 0);
			if (seen == 0 || static_cast<UInt32>(seen) == returnAddress) {
				InterlockedIncrement64(&counts[i]);
				return;
			}
		}
		InterlockedIncrement64(&others);
	}

	// Reads the counts into the plain shape and starts the next window.
	probe::CallCounts TakeSnapshot() {
		probe::CallCounts out;
		out.calls = static_cast<std::uint64_t>(InterlockedExchange64(&calls, 0));
		for (unsigned i = 0; i < probe::ReturnSites::kSites; ++i) {
			const LONG address = addresses[i];
			if (address == 0) {
				break;
			}
			const LONG64 count = InterlockedExchange64(&counts[i], 0);
			if (count != 0) {
				out.sites.sites[out.sites.used++] = probe::ReturnSites::Site{static_cast<std::uint32_t>(address),
				                                                            static_cast<std::uint64_t>(count)};
			}
		}
		out.sites.others = static_cast<std::uint64_t>(InterlockedExchange64(&others, 0));
		return out;
	}
};

AtomicCounts g_counts[static_cast<int>(Probed::Count)];
LogFn g_log = nullptr;

template <class T>
T ReadAt(std::uintptr_t address) {
	return *reinterpret_cast<volatile const T*>(address);
}

// ---- The pass steps: four timed detours (slice pass-drivers, 2026-10-30) ----------
//
// Each detour reads the TSC before and after the original (RDTSC, no
// serialisation: the error is a few cycles against calls of microseconds)
// and adds the difference and the call interlocked. The times are
// inclusive and nested: FinishAccumulating holds every draw of the pass,
// ApplyConstantMap holds SetVertexShaderConstantArray and
// SetVertexShaderConstant. The TSC is calibrated against the performance
// counter over each summary window (probe::TicksPerMillisecond).
enum class Step : int {
	FinishAccumulating = 0,      // 0x007AE070, BSShaderAccumulator slot +0x50 (re/functions/0x007AC9A0.md C2-C4); by return site
	DoRenderShape,               // 0x007693E0, NiDX9Renderer::Do_RenderShape (re/functions/0x007693E0.md): every shape draw
	DrawSkinnedPrimitive,        // 0x00767520 (re/functions/0x00767520.md): the skinned partitions' draws, slots 13/15/14 inside
	SetupTransformations,        // 0x007C9230, shader slot 13 (re/functions/0x007C9230.md): per partition, the light constants inside
	PrepareGeometry,             // 0x0077A310, shader slot 15 (re/functions/0x0077A310.md): the packing
	SetupShaderPrograms,         // 0x0077A1F0, shader slot 14 (re/functions/0x0077A1F0.md): the constant maps inside
	ApplyConstantMap,            // 0x009A8E30, NiD3DShaderConstantMap slot +0x40 (re/functions/0x009A56D0.md)
	SetVertexShaderConstantArray,  // 0x00783180, NiD3DVertexShader slot 11 (re/functions/0x007830C0.md C2)
	SetVertexShaderConstant,     // 0x007830C0, NiD3DVertexShader slot 10 (re/functions/0x007830C0.md)
	Count,
};
const char* const kStepNames[] = {"FinishAccumulating 0x007AE070", "Do_RenderShape 0x007693E0", "DrawSkinnedPrimitive 0x00767520",
                                  "SetupTransformations 0x007C9230", "PrepareGeometry 0x0077A310", "SetupShaderPrograms 0x0077A1F0",
                                  "ApplyConstantMap 0x009A8E30", "SetVertexShaderConstantArray 0x00783180",
                                  "SetVertexShaderConstant 0x007830C0"};
static_assert(sizeof(kStepNames) / sizeof(kStepNames[0]) == static_cast<int>(Step::Count), "one name per step");

// The entries (objdump of the reference EXE, 2026-10-30): whole instructions,
// no rel32; no jump in any body targets the bytes after the entry.
constexpr UInt32 kFinish = 0x007AE070;
constexpr UInt8 kFinishBytes[] = {0x6A, 0xFF, 0x68, 0xD3, 0xD4, 0x9C, 0x00};  // push -1; push 0x009CD4D3 (the SEH frame)
constexpr UInt32 kApplyMap = 0x009A8E30;
constexpr UInt8 kApplyMapBytes[] = {0x83, 0xEC, 0x08, 0x55, 0x8B, 0x6C, 0x24, 0x10};  // sub esp,8; push ebp; mov ebp,[esp+10h]
constexpr UInt32 kSetArray = 0x00783180;
constexpr UInt8 kSetArrayBytes[] = {0x51, 0x80, 0x3D, 0x5B, 0x29, 0xB4, 0x00, 0x00};  // push ecx; cmp byte [0xB4295B],0
constexpr UInt32 kSetConstant = 0x007830C0;
constexpr UInt8 kSetConstantBytes[] = {0x53, 0x8B, 0x5C, 0x24, 0x0C};  // push ebx; mov ebx,[esp+0Ch]
constexpr UInt32 kRenderShape = 0x007693E0;
constexpr UInt8 kRenderShapeBytes[] = {0x83, 0xEC, 0x4C, 0x53, 0x8B, 0xD9};  // sub esp,4Ch; push ebx; mov ebx,ecx
constexpr UInt32 kDrawSkinned = 0x00767520;
constexpr UInt8 kDrawSkinnedBytes[] = {0x8B, 0x44, 0x24, 0x0C, 0x83, 0xEC, 0x1C};  // mov eax,[esp+0Ch]; sub esp,1Ch
constexpr UInt32 kPrepare = 0x0077A310;
constexpr UInt8 kPrepareBytes[] = {0x8B, 0x54, 0x24, 0x04, 0x85, 0xD2};  // mov edx,[esp+4]; test edx,edx
constexpr UInt32 kPrograms = 0x0077A1F0;
constexpr UInt8 kProgramsBytes[] = {0x53, 0x55, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14};  // push ebx; push ebp; push esi; push edi; mov edi,[esp+14h]

// __fastcall with a dummy edx mirrors __thiscall; the stack argument counts
// are the originals' RET immediates (plain RET, 0x2C, 0x14, 0xC; read).
using FinishFn = UInt32(__fastcall*)(void* self, void* edx);
using ApplyMapFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32,
                                       UInt32, UInt32, UInt32);
using SetArrayFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32, UInt32);
using SetConstantFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32);
using RenderShapeFn = UInt32(__fastcall*)(void* self, void* edx, UInt32);                                   // RET 4 (read)
using DrawSkinnedFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32, UInt32);  // RET 0x14 (read)
using PrepareFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32);              // RET 0x10 (read)
using ProgramsFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32);  // RET 0x20 (read)

FinishFn g_finish = nullptr;
ApplyMapFn g_applyMap = nullptr;
SetArrayFn g_setArray = nullptr;
SetConstantFn g_setConstant = nullptr;
RenderShapeFn g_renderShape = nullptr;
DrawSkinnedFn g_drawSkinned = nullptr;
SetupFn g_timedSetup = nullptr;
PrepareFn g_prepare = nullptr;
ProgramsFn g_programs = nullptr;

struct AtomicTiming {
	volatile LONG64 calls = 0;
	volatile LONG64 ticks = 0;

	void Add(unsigned __int64 elapsed) {
		InterlockedIncrement64(&calls);
		InterlockedAdd64(&ticks, static_cast<LONG64>(elapsed));
	}
	probe::StepTiming TakeSnapshot() {
		probe::StepTiming out;
		out.calls = static_cast<std::uint64_t>(InterlockedExchange64(&calls, 0));
		out.ticks = static_cast<std::uint64_t>(InterlockedExchange64(&ticks, 0));
		return out;
	}
};

// The finish split by return site: a slot is claimed with a compare-exchange
// on its address, as AtomicCounts does; eighteen calls a frame, so the scan
// is cheap.
struct AtomicSiteTiming {
	volatile LONG addresses[probe::StepSites::kSites] = {};
	AtomicTiming timings[probe::StepSites::kSites];
	AtomicTiming others;

	void Add(UInt32 returnAddress, unsigned __int64 elapsed) {
		for (unsigned i = 0; i < probe::StepSites::kSites; ++i) {
			const LONG seen = InterlockedCompareExchange(&addresses[i], static_cast<LONG>(returnAddress), 0);
			if (seen == 0 || static_cast<UInt32>(seen) == returnAddress) {
				timings[i].Add(elapsed);
				return;
			}
		}
		others.Add(elapsed);
	}
	probe::StepSites TakeSnapshot() {
		probe::StepSites out;
		for (unsigned i = 0; i < probe::StepSites::kSites; ++i) {
			const LONG address = addresses[i];
			if (address == 0) {
				break;
			}
			const probe::StepTiming t = timings[i].TakeSnapshot();
			if (t.calls != 0) {
				out.sites[out.used++] = probe::StepSites::Site{static_cast<std::uint32_t>(address), t};
			}
		}
		out.others = others.TakeSnapshot();
		return out;
	}
};

AtomicTiming g_steps[static_cast<int>(Step::Count)];
AtomicSiteTiming g_finishSites;
bool g_stepInstalled[static_cast<int>(Step::Count)] = {};
bool g_stepsOn = false;
probe::ClockPair g_stepClock;  // the clocks at the last summary (0: none yet)

volatile LONG g_passFromSteps = 0;  // forwarded to the uploads probe's counter below
void CountPassFromSteps();

UInt32 __fastcall TimeFinish(void* self, void* /*edx*/) {
	CountPassFromSteps();
	const UInt32 site = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_finish(self, nullptr);
	const unsigned __int64 elapsed = __rdtsc() - start;
	g_steps[static_cast<int>(Step::FinishAccumulating)].Add(elapsed);
	g_finishSites.Add(site, elapsed);
	return eax;
}

UInt32 __fastcall TimeRenderShape(void* self, void* /*edx*/, UInt32 a1) {
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_renderShape(self, nullptr, a1);
	g_steps[static_cast<int>(Step::DoRenderShape)].Add(__rdtsc() - start);
	return eax;
}

UInt32 __fastcall TimeDrawSkinned(void* self, void* /*edx*/, UInt32 a1, UInt32 a2, UInt32 a3, UInt32 a4, UInt32 a5) {
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_drawSkinned(self, nullptr, a1, a2, a3, a4, a5);
	g_steps[static_cast<int>(Step::DrawSkinnedPrimitive)].Add(__rdtsc() - start);
	return eax;
}

UInt32 __fastcall TimeSetup(void* self, void* /*edx*/, UInt32 a1, UInt32 a2, UInt32 a3, UInt32 a4, UInt32 a5, UInt32 a6, UInt32 a7,
                            UInt32 a8) {
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_timedSetup(self, nullptr, a1, a2, a3, a4, a5, a6, a7, a8);
	g_steps[static_cast<int>(Step::SetupTransformations)].Add(__rdtsc() - start);
	return eax;
}

UInt32 __fastcall TimePrepare(void* self, void* /*edx*/, UInt32 a1, UInt32 a2, UInt32 a3, UInt32 a4) {
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_prepare(self, nullptr, a1, a2, a3, a4);
	g_steps[static_cast<int>(Step::PrepareGeometry)].Add(__rdtsc() - start);
	return eax;
}

UInt32 __fastcall TimePrograms(void* self, void* /*edx*/, UInt32 a1, UInt32 a2, UInt32 a3, UInt32 a4, UInt32 a5, UInt32 a6, UInt32 a7,
                               UInt32 a8) {
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_programs(self, nullptr, a1, a2, a3, a4, a5, a6, a7, a8);
	g_steps[static_cast<int>(Step::SetupShaderPrograms)].Add(__rdtsc() - start);
	return eax;
}

UInt32 __fastcall TimeApplyMap(void* self, void* /*edx*/, UInt32 a1, UInt32 a2, UInt32 a3, UInt32 a4, UInt32 a5, UInt32 a6,
                               UInt32 a7, UInt32 a8, UInt32 a9, UInt32 a10, UInt32 a11) {
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_applyMap(self, nullptr, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
	g_steps[static_cast<int>(Step::ApplyConstantMap)].Add(__rdtsc() - start);
	return eax;
}

UInt32 __fastcall TimeSetArray(void* self, void* /*edx*/, UInt32 a1, UInt32 a2, UInt32 a3, UInt32 a4, UInt32 a5) {
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_setArray(self, nullptr, a1, a2, a3, a4, a5);
	g_steps[static_cast<int>(Step::SetVertexShaderConstantArray)].Add(__rdtsc() - start);
	return eax;
}

UInt32 __fastcall TimeSetConstant(void* self, void* /*edx*/, UInt32 a1, UInt32 a2, UInt32 a3) {
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_setConstant(self, nullptr, a1, a2, a3);
	g_steps[static_cast<int>(Step::SetVertexShaderConstant)].Add(__rdtsc() - start);
	return eax;
}

probe::ClockPair ReadClocks();
std::uint64_t ClockFrequency();
// ---- The packing's cross-frame probe (docs/subsystems/stream-packing.md, L3) -------
//
// Two detours: the packer 0x00763130 (thiscall(renderer; record, geometry,
// skin, partition, decl, force), RET 0x18; re/functions/0x00763130.md) puts
// (geometry, partition) and the data's consistency word into this thread's
// slot for the duration of the call; the unlock 0x00776D80 (thiscall(chip;
// d3dBuffer), RET 4; read with objdump 2026-10-30: memcpy([chip+0x48],
// [chip+0x40], [chip+0x4C]) then the D3D unlock) hashes the staging bytes
// [chip+0x40] of length [chip+0x4C] before the original copies them, and
// files the hash under (geometry, partition, chip). Unlocks with no packer
// on this thread's stack are only counted.
constexpr UInt32 kPacker = 0x00763130;
constexpr UInt8 kPackerBytes[] = {0x83, 0xEC, 0x1C, 0x53, 0x56};  // sub esp,1Ch; push ebx; push esi
constexpr UInt32 kUnlock = 0x00776D80;
constexpr UInt8 kUnlockBytes[] = {0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x4C};  // push esi; mov esi,ecx; mov eax,[esi+4Ch]
// The unlock's `this` is the declaration's buffer manager (PackStream's
// [this+0x0C], re/functions/0x0076FE20.md), whose +0x40 holds the staging
// pointer and +0x4C the bytes. The chip whose range was locked is PackStream's
// fifth argument; PackStream passes (chip+0x08 vb, +0x0C offset, +0x14 size,
// +0x10 flags) to the lock (read there, 0x0076FF70..0x0076FF79). A third
// detour on PackStream 0x0076FE20 (thiscall(decl; geometry, skin, partition,
// dirty, chip, streamIndex, lockedDest), RET 0x1C; entry bytes read with
// objdump, no jump into them) keeps the chip and the stream index for the
// thread, so the key is (geometry, partition, stream) and the slice the
// chip's own (buffer, offset, size).
constexpr UInt32 kManagerStagingOffset = 0x40;
constexpr UInt32 kManagerBytesOffset = 0x4C;
constexpr UInt32 kChipBufferOffset = 0x08;
constexpr UInt32 kChipOffsetOffset = 0x0C;
constexpr UInt32 kChipFlagsOffset = 0x10;
constexpr UInt32 kChipSizeOffset = 0x14;
constexpr UInt32 kPackStream = 0x0076FE20;
constexpr UInt8 kPackStreamBytes[] = {0x83, 0xEC, 0x38, 0x53, 0x8B, 0x5C, 0x24, 0x40};  // sub esp,38h; push ebx; mov ebx,[esp+40h]
using PackStreamFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32);
PackStreamFn g_packStream = nullptr;
constexpr UInt32 kGeometryConsistencyOffset = 0x2E;

using PackerFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32);
using UnlockFn = UInt32(__fastcall*)(void* self, void* edx, UInt32);

PackerFn g_packer = nullptr;
UnlockFn g_unlock = nullptr;
bool g_packCacheOn = false;

struct PackContext {
	UInt32 depth = 0;
	UInt32 geometry = 0;
	UInt32 partition = 0;
	UInt32 chip = 0;    // PackStream's chip while it runs on this thread, else 0
	UInt32 stream = 0;  // its stream index
	UInt32 decl = 0;    // the packer's declaration argument
	bool volatileData = false;  // the data's consistency nibble at the packer's entry was 0x8000
};
range_walk_game::ThreadSlots<PackContext> g_packContexts;
SRWLOCK g_packLock = SRWLOCK_INIT;
probe::PackCache* g_packCache = nullptr;  // 65 536 slots, made at install
probe::PackStats g_packStats;
probe::ClockPair g_packClock;  // the clocks at the last pack summary

UInt32 __fastcall ProbePacker(void* self, void* /*edx*/, UInt32 record, UInt32 geometry, UInt32 skin, UInt32 partition, UInt32 decl,
                              UInt32 force) {
	PackContext& context = g_packContexts.Get();
	const unsigned __int64 start = __rdtsc();
	if (context.depth++ == 0) {
		context.geometry = geometry;
		context.partition = partition;
		context.decl = decl;
		// The original tests the record and the geometry for null before it
		// reads either (JE 0x007633BD at 0x0076313F and 0x0076314B, read):
		// read the consistency word only where it would.
		context.volatileData = false;
		if (record != 0 && geometry != 0) {
			const auto consistency = *reinterpret_cast<volatile UInt16*>(static_cast<std::uintptr_t>(geometry) + kGeometryConsistencyOffset);
			context.volatileData = (consistency & 0xF000u) == 0x8000u;
			AcquireSRWLockExclusive(&g_packLock);
			probe::CountConsistency(consistency, g_packStats);
			ReleaseSRWLockExclusive(&g_packLock);
		}
	}
	const UInt32 eax = g_packer(self, nullptr, record, geometry, skin, partition, decl, force);
	if (--context.depth == 0 && context.volatileData) {
		const unsigned __int64 elapsed = __rdtsc() - start;
		AcquireSRWLockExclusive(&g_packLock);
		++g_packStats.volatileCalls;
		g_packStats.volatileTicks += elapsed;
		ReleaseSRWLockExclusive(&g_packLock);
	}
	return eax;
}

UInt32 __fastcall ProbePackStream(void* self, void* /*edx*/, UInt32 geometry, UInt32 skin, UInt32 partition, UInt32 dirty, UInt32 chip,
                                  UInt32 stream, UInt32 lockedDest) {
	PackContext& context = g_packContexts.Get();
	const UInt32 chipBefore = context.chip;
	const UInt32 streamBefore = context.stream;
	context.chip = chip;
	context.stream = stream;
	const UInt32 eax = g_packStream(self, nullptr, geometry, skin, partition, dirty, chip, stream, lockedDest);
	context.chip = chipBefore;
	context.stream = streamBefore;
	return eax;
}

UInt32 __fastcall ProbeUnlock(void* self, void* /*edx*/, UInt32 d3dBuffer) {
	PackContext* context = g_packContexts.Existing();
	if (context == nullptr || context->depth == 0) {
		AcquireSRWLockExclusive(&g_packLock);
		++g_packStats.outside;
		ReleaseSRWLockExclusive(&g_packLock);
		return g_unlock(self, nullptr, d3dBuffer);
	}
	const auto manager = reinterpret_cast<std::uintptr_t>(self);
	const auto staging = *reinterpret_cast<const unsigned char* volatile*>(manager + kManagerStagingOffset);
	const UInt32 bytes = *reinterpret_cast<volatile UInt32*>(manager + kManagerBytesOffset);
	const unsigned __int64 hashStart = __rdtsc();
	const std::uint64_t hash = staging != nullptr ? probe::HashBytes(staging, bytes) : 0;
	const unsigned __int64 hashTicks = __rdtsc() - hashStart;
	// The slice from the chip PackStream is packing into (its buffer should be
	// the vb the unlock receives); without a chip on this thread the vb alone.
	probe::PackSlice slice{d3dBuffer, 0, bytes};
	UInt32 flags = 0;
	if (context->chip != 0) {
		const std::uintptr_t chip = context->chip;
		slice = probe::PackSlice{ReadAt<UInt32>(chip + kChipBufferOffset), ReadAt<UInt32>(chip + kChipOffsetOffset), ReadAt<UInt32>(chip + kChipSizeOffset)};
		flags = ReadAt<UInt32>(chip + kChipFlagsOffset);
	}
	AcquireSRWLockExclusive(&g_packLock);
	const probe::PackVerdict verdict =
	    g_packCache->Add(probe::PackKey{context->geometry, context->partition, context->stream, context->decl}, hash, bytes, g_packStats, slice);
	probe::CountVerdictByConsistency(verdict, context->volatileData, g_packStats);
	probe::CountLockFlags(flags, g_packStats);
	g_packStats.hashTicks += hashTicks;
	ReleaseSRWLockExclusive(&g_packLock);
	return g_unlock(self, nullptr, d3dBuffer);
}

UInt32 InstallPackCache(LogFn log) {
	if (!g_packContexts.Allocate()) {
		log("Probe: pack cache - no TLS slot; not installed");
		return 0;
	}
	g_packCache = new probe::PackCache(16);
	UInt32 made = 0;
	made += InstallEntryDetour("PackSkinnedGeometryBuffer 0x00763130 (pack cache)", kPacker, kPackerBytes, sizeof(kPackerBytes),
	                           reinterpret_cast<const void*>(&ProbePacker), reinterpret_cast<void**>(&g_packer), log) ? 1 : 0;
	made += InstallEntryDetour("dynamic buffer unlock 0x00776D80 (pack cache)", kUnlock, kUnlockBytes, sizeof(kUnlockBytes),
	                           reinterpret_cast<const void*>(&ProbeUnlock), reinterpret_cast<void**>(&g_unlock), log) ? 1 : 0;
	made += InstallEntryDetour("PackStream 0x0076FE20 (pack cache)", kPackStream, kPackStreamBytes, sizeof(kPackStreamBytes),
	                           reinterpret_cast<const void*>(&ProbePackStream), reinterpret_cast<void**>(&g_packStream), log) ? 1 : 0;
	g_packCacheOn = made == 3;
	g_packClock = ReadClocks();
	if (made != 3) {
		log("Probe: pack cache - %u of 3 detours made; the counts need all three, so it is off", made);
	}
	return made;
}

void LogPackCache() {
	AcquireSRWLockExclusive(&g_packLock);
	const probe::PackStats stats = g_packStats;
	g_packStats.Reset();
	const std::uint64_t keys = g_packCache->Keys();
	ReleaseSRWLockExclusive(&g_packLock);
	char line[600];
	probe::FormatPackStats(stats, keys, line, sizeof(line));
	g_log("%s", line);
	{
		const probe::ClockPair now = ReadClocks();
		const double ticksPerMs = probe::TicksPerMillisecond(g_packClock, now, ClockFrequency());
		g_packClock = now;
		int at = std::snprintf(line, sizeof(line), "Probe: pack cache time (%.0f ticks per ms) -", ticksPerMs);
		probe::FormatVolatilePackTime(stats, ticksPerMs, line + at, sizeof(line) - at);
		g_log("%s", line);
	}
	AcquireSRWLockExclusive(&g_packLock);
	const probe::PackCache::KeyShape shape = g_packCache->Shape();
	int at = std::snprintf(line, sizeof(line), "Probe: pack cache keys - always same %llu, always changed %llu, mixed %llu, untested %llu, distinct buffers %llu; changed keys (stream same/changed):",
	                       static_cast<unsigned long long>(shape.alwaysSame), static_cast<unsigned long long>(shape.alwaysChanged),
	                       static_cast<unsigned long long>(shape.mixed), static_cast<unsigned long long>(shape.untested),
	                       static_cast<unsigned long long>(shape.buffers));
	g_packCache->VisitChangedKeys(12, [&](probe::PackKey key, std::uint64_t same, std::uint64_t changed) {
		if (at > 0 && at < 520) {
			at += std::snprintf(line + at, sizeof(line) - at, " %08X/%08X s%u d%08X %llu/%llu", key.geometry, key.partition, key.chip, key.decl,
			                    static_cast<unsigned long long>(same), static_cast<unsigned long long>(changed));
		}
	});
	ReleaseSRWLockExclusive(&g_packLock);
	g_log("%s", line);
}

// ---- The constant uploads' redundancy probe ----------------------------------------
//
// Three detours: SetVertexShaderConstant 0x007830C0 (thiscall(program; entry,
// data, count), RET 0xC: data defaults to [entry+0x30], count to [entry+0x20]
// or 1, the register is [entry+0x1C]; re/functions/0x007830C0.md),
// SetVertexShaderConstantArray 0x00783180 (thiscall(program; entry, base,
// count, stride, indices), RET 0x14: bone i goes to reg0 + i*stride from
// base + indices[i]*stride*16, stride vec4s; re/functions/0x00783180.md), and
// the accumulator's finish 0x007AE070 as the pass counter (shared with the
// pass steps when those are on). The hashes are taken before the original
// runs, from the same memory it reads.
constexpr UInt32 kEntryTypeOffset = 0x14;
constexpr UInt32 kEntryRegisterOffset = 0x1C;
constexpr UInt32 kEntryCountOffset = 0x20;
constexpr UInt32 kEntryDataOffset = 0x30;
constexpr UInt32 kTypeTable = 0x00B428D8;
constexpr UInt32 kTypeTableReady = 0x00B4295B;

SetConstantFn g_uploadSetConstant = nullptr;
SetArrayFn g_uploadSetArray = nullptr;
FinishFn g_uploadFinish = nullptr;
bool g_uploadsOn = false;
SRWLOCK g_uploadLock = SRWLOCK_INIT;
probe::RegisterCache* g_registers = nullptr;
probe::UploadStats g_uploadStats;
volatile LONG g_pass = 0;


UInt32 __fastcall UploadSetConstant(void* self, void* /*edx*/, UInt32 entry, UInt32 data, UInt32 count) {
	if (entry != 0) {
		const UInt32 reg = ReadAt<UInt32>(entry + kEntryRegisterOffset);
		const UInt32 source = data != 0 ? data : ReadAt<UInt32>(entry + kEntryDataOffset);
		UInt32 vec4s = count != 0 ? count : ReadAt<UInt32>(entry + kEntryCountOffset);
		if (vec4s == 0) {
			vec4s = 1;
		}
		const UInt32 type = ReadAt<UInt32>(entry + kEntryTypeOffset) >> 28;
		if (source != 0 && vec4s < 4096) {
			const UInt32 bytes = vec4s * 16;
			const std::uint64_t hash = probe::HashBytes(reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(source)), bytes);
			AcquireSRWLockExclusive(&g_uploadLock);
			g_registers->Add(reg, hash, bytes, static_cast<UInt32>(g_pass), g_uploadStats);
			++g_uploadStats.byType[type & 7u];
			ReleaseSRWLockExclusive(&g_uploadLock);
		}
	}
	return g_uploadSetConstant(self, nullptr, entry, data, count);
}

UInt32 __fastcall UploadSetArray(void* self, void* /*edx*/, UInt32 entry, UInt32 base, UInt32 count, UInt32 stride, UInt32 indices) {
	// The original's type check: the table value of the entry's type byte is 8 or 9
	// (0x007831A2 / 0x007831C3, read); the table is filled lazily by 0x00783C70.
	bool passes = false;
	if (entry != 0 && ReadAt<UInt8>(kTypeTableReady) != 0) {
		const UInt32 typeByte = ReadAt<UInt32>(entry + kEntryTypeOffset) & 0xFFu;
		const UInt32 mapped = ReadAt<UInt32>(kTypeTable + 4 * typeByte);
		passes = mapped == 8 || mapped == 9;
	}
	if (passes && base != 0 && indices != 0 && stride >= 1 && stride <= 4 && count <= 256) {
		const UInt32 reg0 = ReadAt<UInt32>(entry + kEntryRegisterOffset);
		const auto index = reinterpret_cast<const std::uint16_t*>(static_cast<std::uintptr_t>(indices));
		const UInt32 bytesPerBone = stride * 16;
		AcquireSRWLockExclusive(&g_uploadLock);
		++g_uploadStats.paletteCalls;
		g_uploadStats.bones += count;
		if (probe::ConsecutiveRun(index, count)) {
			++g_uploadStats.consecutivePalettes;
		}
		for (UInt32 i = 0; i < count; ++i) {
			const auto rows = reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(base) + static_cast<std::uintptr_t>(index[i]) * bytesPerBone);
			g_registers->Add(reg0 + i * stride, probe::HashBytes(rows, bytesPerBone), bytesPerBone, static_cast<UInt32>(g_pass), g_uploadStats);
		}
		ReleaseSRWLockExclusive(&g_uploadLock);
	}
	return g_uploadSetArray(self, nullptr, entry, base, count, stride, indices);
}

UInt32 __fastcall UploadFinish(void* self, void* /*edx*/) {
	InterlockedIncrement(&g_pass);
	return g_uploadFinish(self, nullptr);
}

void CountPassFromSteps() {
	InterlockedIncrement(&g_pass);
}

// withFinish: false when the pass steps hold the finish's entry (their detour
// then counts the passes).
UInt32 InstallConstantUploads(bool withFinish, LogFn log) {
	g_registers = new probe::RegisterCache();
	UInt32 made = 0;
	made += InstallEntryDetour("SetVertexShaderConstant 0x007830C0 (uploads)", kSetConstant, kSetConstantBytes, sizeof(kSetConstantBytes),
	                           reinterpret_cast<const void*>(&UploadSetConstant), reinterpret_cast<void**>(&g_uploadSetConstant), log) ? 1 : 0;
	made += InstallEntryDetour("SetVertexShaderConstantArray 0x00783180 (uploads)", kSetArray, kSetArrayBytes, sizeof(kSetArrayBytes),
	                           reinterpret_cast<const void*>(&UploadSetArray), reinterpret_cast<void**>(&g_uploadSetArray), log) ? 1 : 0;
	if (withFinish) {
		made += InstallEntryDetour("FinishAccumulating 0x007AE070 (uploads: the pass counter)", kFinish, kFinishBytes, sizeof(kFinishBytes),
		                           reinterpret_cast<const void*>(&UploadFinish), reinterpret_cast<void**>(&g_uploadFinish), log) ? 1 : 0;
	}
	g_uploadsOn = made >= 2;
	return made;
}

void LogConstantUploads() {
	AcquireSRWLockExclusive(&g_uploadLock);
	const probe::UploadStats stats = g_uploadStats;
	g_uploadStats.Reset();
	ReleaseSRWLockExclusive(&g_uploadLock);
	char line[500];
	probe::FormatUploadStats(stats, line, sizeof(line));
	g_log("%s", line);
}

probe::ClockPair ReadClocks() {
	LARGE_INTEGER qpc{};
	QueryPerformanceCounter(&qpc);
	return probe::ClockPair{static_cast<std::uint64_t>(__rdtsc()), static_cast<std::uint64_t>(qpc.QuadPart)};
}

std::uint64_t ClockFrequency() {
	LARGE_INTEGER f{};
	QueryPerformanceFrequency(&f);
	return static_cast<std::uint64_t>(f.QuadPart);
}

// withSetup: false when the call counters hold 0x007C9230's entry already.
UInt32 InstallPassSteps(bool withSetup, LogFn log) {
	struct Entry {
		Step which;
		UInt32 address;
		const UInt8* bytes;
		UInt32 length;
		const void* replacement;
		void** original;
	};
	const Entry entries[] = {
	    {Step::FinishAccumulating, kFinish, kFinishBytes, sizeof(kFinishBytes), reinterpret_cast<const void*>(&TimeFinish), reinterpret_cast<void**>(&g_finish)},
	    {Step::DoRenderShape, kRenderShape, kRenderShapeBytes, sizeof(kRenderShapeBytes), reinterpret_cast<const void*>(&TimeRenderShape), reinterpret_cast<void**>(&g_renderShape)},
	    {Step::DrawSkinnedPrimitive, kDrawSkinned, kDrawSkinnedBytes, sizeof(kDrawSkinnedBytes), reinterpret_cast<const void*>(&TimeDrawSkinned), reinterpret_cast<void**>(&g_drawSkinned)},
	    {Step::SetupTransformations, kSetup, kSetupBytes, sizeof(kSetupBytes), reinterpret_cast<const void*>(&TimeSetup), reinterpret_cast<void**>(&g_timedSetup)},
	    {Step::PrepareGeometry, kPrepare, kPrepareBytes, sizeof(kPrepareBytes), reinterpret_cast<const void*>(&TimePrepare), reinterpret_cast<void**>(&g_prepare)},
	    {Step::SetupShaderPrograms, kPrograms, kProgramsBytes, sizeof(kProgramsBytes), reinterpret_cast<const void*>(&TimePrograms), reinterpret_cast<void**>(&g_programs)},
	    {Step::ApplyConstantMap, kApplyMap, kApplyMapBytes, sizeof(kApplyMapBytes), reinterpret_cast<const void*>(&TimeApplyMap), reinterpret_cast<void**>(&g_applyMap)},
	    {Step::SetVertexShaderConstantArray, kSetArray, kSetArrayBytes, sizeof(kSetArrayBytes), reinterpret_cast<const void*>(&TimeSetArray), reinterpret_cast<void**>(&g_setArray)},
	    {Step::SetVertexShaderConstant, kSetConstant, kSetConstantBytes, sizeof(kSetConstantBytes), reinterpret_cast<const void*>(&TimeSetConstant), reinterpret_cast<void**>(&g_setConstant)},
	};
	UInt32 made = 0;
	for (const Entry& e : entries) {
		if (e.which == Step::SetupTransformations && !withSetup) {
			log("Probe: SetupTransformations 0x007C9230 timed through the call counter's entry is not possible - its time is left out");
			continue;
		}
		const bool ok = InstallEntryDetour(kStepNames[static_cast<int>(e.which)], e.address, e.bytes, e.length, e.replacement, e.original, log);
		g_stepInstalled[static_cast<int>(e.which)] = ok;
		made += ok ? 1 : 0;
	}
	g_stepsOn = made != 0;
	g_stepClock = ReadClocks();
	return made;
}

// "Probe: pass steps (N ticks per ms) - name N calls, X ms (Y us per call); ..."
void LogPassSteps() {
	const probe::ClockPair now = ReadClocks();
	const double ticksPerMs = probe::TicksPerMillisecond(g_stepClock, now, ClockFrequency());
	g_stepClock = now;
	char line[1200];
	int at = std::snprintf(line, sizeof(line), "Probe: pass steps (%.0f ticks per ms) -", ticksPerMs);
	for (int i = 0; i < static_cast<int>(Step::Count) && at < 1000; ++i) {
		if (g_stepInstalled[i]) {
			at += probe::FormatStepTiming(kStepNames[i], g_steps[i].TakeSnapshot(), ticksPerMs, line + at, sizeof(line) - at);
		}
	}
	g_log("%s", line);
	if (g_stepInstalled[static_cast<int>(Step::FinishAccumulating)]) {
		at = std::snprintf(line, sizeof(line), "Probe: pass steps -");
		probe::FormatStepSites(kStepNames[static_cast<int>(Step::FinishAccumulating)], g_finishSites.TakeSnapshot(), ticksPerMs, line + at,
		                       sizeof(line) - at);
		g_log("%s", line);
	}
}
// IsInRange split by the light's path: [this+0x14C] != 0 selects the six-plane
// test, 0 the distance test (re/functions/0x007D2EA0.md B3). The split by
// return site says which caller's lights take the plane path - the question
// behind the unrestored mask under 0x007C6100 (knowledge/culling-and-draw-setup.md).
constexpr UInt32 kPlaneSourceOffset = 0x14C;
AtomicCounts g_inRangePlanePath;
AtomicCounts g_inRangeDistancePath;
bool g_installed[static_cast<int>(Probed::Count)] = {};
bool g_memory = false;
probe::MemoryPeak g_memoryPeak;

void Count(Probed which, void* returnAddress) {
	g_counts[static_cast<int>(which)].Add(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(returnAddress)));
}

UInt32 __fastcall ProbeSetup(void* self, void* /*edx*/, UInt32 a1, UInt32 a2, UInt32 a3, UInt32 a4, UInt32 a5, UInt32 a6,
                             UInt32 a7, UInt32 a8) {
	Count(Probed::SetupTransformations, _ReturnAddress());
	// Once per draw: cheap enough to drive the summary clock from here, so
	// the probes report even when no shadow runs every frame.
	SummaryIfDue(GetTickCount(), nullptr, false);
	return g_setup(self, nullptr, a1, a2, a3, a4, a5, a6, a7, a8);
}

void __fastcall ProbeCull(void* self, void* /*edx*/, UInt32 object) {
	Count(Probed::Cull, _ReturnAddress());
	g_cull(self, nullptr, object);
}

UInt8 __fastcall ProbeInRange(void* self, void* /*edx*/, UInt32 object, UInt32 light) {
	Count(Probed::IsInRange, _ReturnAddress());
	const UInt32 returnAddress = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
	const UInt32 planeSource = *reinterpret_cast<volatile UInt32*>(reinterpret_cast<std::uintptr_t>(self) + kPlaneSourceOffset);
	(planeSource != 0 ? g_inRangePlanePath : g_inRangeDistancePath).Add(returnAddress);
	return g_inRange(self, nullptr, object, light);
}

UInt32 __cdecl ProbeCopy(const void* desc) {
	Count(Probed::CopyVertexStreamElements, _ReturnAddress());
	return g_copy(desc);
}

int __fastcall ProbeWhichSide(const void* sphere, void* /*edx*/, const void* plane) {
	Count(Probed::WhichSide, _ReturnAddress());
	return g_whichSide(sphere, nullptr, plane);
}

struct ProcessMemoryCounters {
	DWORD cb;
	DWORD pageFaultCount;
	SIZE_T peakWorkingSetSize;
	SIZE_T workingSetSize;
	SIZE_T quotaPeakPagedPoolUsage;
	SIZE_T quotaPagedPoolUsage;
	SIZE_T quotaPeakNonPagedPoolUsage;
	SIZE_T quotaNonPagedPoolUsage;
	SIZE_T pagefileUsage;
	SIZE_T peakPagefileUsage;
};
using GetProcessMemoryInfoFn = BOOL(WINAPI*)(HANDLE process, ProcessMemoryCounters* counters, DWORD cb);

bool SampleMemory(probe::MemorySample* out) {
	static GetProcessMemoryInfoFn s_getProcessMemoryInfo = reinterpret_cast<GetProcessMemoryInfoFn>(
	    GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo"));
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	if (!GlobalMemoryStatusEx(&status)) {
		return false;
	}
	out->virtualTotalBytes = status.ullTotalVirtual;
	out->virtualAvailableBytes = status.ullAvailVirtual;
	ProcessMemoryCounters counters{};
	counters.cb = sizeof(counters);
	if (s_getProcessMemoryInfo != nullptr && s_getProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
		out->commitBytes = counters.pagefileUsage;
		out->workingSetBytes = counters.workingSetSize;
		out->peakWorkingSetBytes = counters.peakWorkingSetSize;
	}
	return true;
}

}  // namespace

UInt32 ProbesAsked(const ShadowSettings& settings) {
	return (settings.callCounters ? static_cast<UInt32>(Probed::Count) : 0u) +
	       (settings.passSteps ? static_cast<UInt32>(Step::Count) - (settings.callCounters ? 1u : 0u) : 0u) +
	       (settings.packCache ? 3u : 0u) + (settings.constantUploads ? (settings.passSteps ? 2u : 3u) : 0u);
}

UInt32 InstallProbes(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done) {
		return 0;
	}
	s_done = true;
	g_log = log;
	g_memory = settings.memoryProbe;
	UInt32 made = 0;
	if (settings.passSteps) {
		made += InstallPassSteps(!settings.callCounters, log);
	}
	if (settings.packCache) {
		made += InstallPackCache(log);
	}
	if (settings.constantUploads) {
		made += InstallConstantUploads(!settings.passSteps, log);
	}
	if (!settings.callCounters) {
		return made;
	}
	struct Entry {
		Probed which;
		UInt32 address;
		const UInt8* bytes;
		UInt32 length;
		const void* replacement;
		void** original;
	};
	const Entry entries[] = {
	    {Probed::SetupTransformations, kSetup, kSetupBytes, sizeof(kSetupBytes), reinterpret_cast<const void*>(&ProbeSetup), reinterpret_cast<void**>(&g_setup)},
	    {Probed::Cull, kCull, kCullBytes, sizeof(kCullBytes), reinterpret_cast<const void*>(&ProbeCull), reinterpret_cast<void**>(&g_cull)},
	    {Probed::IsInRange, kInRange, kInRangeBytes, sizeof(kInRangeBytes), reinterpret_cast<const void*>(&ProbeInRange), reinterpret_cast<void**>(&g_inRange)},
	    {Probed::CopyVertexStreamElements, kCopy, kCopyBytes, sizeof(kCopyBytes), reinterpret_cast<const void*>(&ProbeCopy), reinterpret_cast<void**>(&g_copy)},
	    {Probed::WhichSide, kWhichSide, kWhichSideBytes, sizeof(kWhichSideBytes), reinterpret_cast<const void*>(&ProbeWhichSide), reinterpret_cast<void**>(&g_whichSide)},
	};
	for (const Entry& e : entries) {
		const bool ok = InstallEntryDetour(kNames[static_cast<int>(e.which)], e.address, e.bytes, e.length, e.replacement, e.original, log);
		g_installed[static_cast<int>(e.which)] = ok;
		made += ok ? 1 : 0;
	}
	return made;
}

void LogProbeSummary() {
	if (g_log == nullptr) {
		return;
	}
	bool anyCounter = false;
	char line[900];
	int at = std::snprintf(line, sizeof(line), "Probe: calls");
	for (int i = 0; i < static_cast<int>(Probed::Count) && at < 800; ++i) {
		if (!g_installed[i]) {
			continue;
		}
		anyCounter = true;
		const probe::CallCounts counts = g_counts[i].TakeSnapshot();
		at += probe::FormatCallCounts(kNames[i], counts, line + at, sizeof(line) - at);
	}
	if (anyCounter) {
		g_log("%s", line);
		if (g_installed[static_cast<int>(Probed::IsInRange)]) {
			at = std::snprintf(line, sizeof(line), "Probe: IsInRange by path -");
			at += probe::FormatCallCounts("plane path (+0x14C != 0)", g_inRangePlanePath.TakeSnapshot(), line + at, sizeof(line) - at);
			at += probe::FormatCallCounts("distance path", g_inRangeDistancePath.TakeSnapshot(), line + at, sizeof(line) - at);
			g_log("%s", line);
		}
	}
	if (g_stepsOn) {
		LogPassSteps();
	}
	if (g_packCacheOn) {
		LogPackCache();
	}
	if (g_uploadsOn) {
		LogConstantUploads();
	}
	if (g_memory) {
		probe::MemorySample sample;
		if (SampleMemory(&sample)) {
			g_memoryPeak.Add(sample);
			probe::FormatMemory(sample, g_memoryPeak, line, sizeof(line));
			g_log("%s", line);
		}
	}
}

}  // namespace obng::plugin
