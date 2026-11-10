// The counting detours and the memory sample. Each detour counts the call
// and its return site, then calls the original with the same arguments; the
// game sees no difference. The counts are kept with interlocked operations,
// because NiBound::WhichSide runs for every plane of every object and must
// not take a lock. The memory sample is taken when the summary is due.

#include "Probes.h"

#include <windows.h>

#include <intrin.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "OcclusionProbe.h"
#include "GrassProbe.h"
#include "ProbeLogic.h"
#include "RangeWalkGame.h"
#include "ShadowRecorder.h"
#include "UnroutedDrawProbe.h"

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

// A step split by return site: a slot is claimed with a compare-exchange on
// its address, as AtomicCounts does. probe::SiteTable<N> is the snapshot.
template <unsigned N>
struct AtomicSiteTiming {
	volatile LONG addresses[N] = {};
	AtomicTiming timings[N];
	AtomicTiming others;

	void Add(UInt32 returnAddress, unsigned __int64 elapsed) {
		for (unsigned i = 0; i < N; ++i) {
			const LONG seen = InterlockedCompareExchange(&addresses[i], static_cast<LONG>(returnAddress), 0);
			if (seen == 0 || static_cast<UInt32>(seen) == returnAddress) {
				timings[i].Add(elapsed);
				return;
			}
		}
		others.Add(elapsed);
	}
	probe::SiteTable<N> TakeSnapshot() {
		probe::SiteTable<N> out;
		for (unsigned i = 0; i < N; ++i) {
			const LONG address = addresses[i];
			if (address == 0) {
				break;
			}
			const probe::StepTiming t = timings[i].TakeSnapshot();
			if (t.calls != 0) {
				out.sites[out.used++] = typename probe::SiteTable<N>::Site{static_cast<std::uint32_t>(address), t};
			}
		}
		out.others = others.TakeSnapshot();
		return out;
	}
};

AtomicTiming g_steps[static_cast<int>(Step::Count)];
AtomicSiteTiming<probe::StepSites::kSites> g_finishSites;  // eighteen calls a frame: the scan is cheap
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
	UInt32 skin = 0;    // the packer's skin instance argument
	std::uint64_t sourceHash = 0;  // the data's vertex and normal arrays at the packer's entry (volatile data only)
	bool volatileData = false;  // the data's consistency nibble at the packer's entry was 0x8000
};
range_walk_game::ThreadSlots<PackContext> g_packContexts;
SRWLOCK g_packLock = SRWLOCK_INIT;
probe::PackCache* g_packCache = nullptr;  // 65 536 slots, made at install
probe::PackStats g_packStats;
probe::ClockPair g_packClock;  // the clocks at the last pack summary

// The change signal under test: BSFaceGenNiNode's gated morph application
// 0x0055D1B0 (re/functions/0x0055D4C0.md step 9: called only when the face
// animation reported a change). Every geometry data under the head node is
// stamped when it runs; a volatile pack is "clean" when its data was not
// stamped since that key's last pack. If the signal is sound, a clean pack
// never carries changed bytes.
// Second candidate (2026-11-09): the modify bracket's opening 0x00728AB0
// (thiscall(byte), RET 4; its twelve callers are the FaceGen and geometry
// writers), stamping the object and the one at its +0x34.
constexpr UInt32 kFaceMorph = 0x00728AB0;
constexpr UInt8 kFaceMorphBytes[] = {0x57, 0x8B, 0xF9, 0x80, 0x7F, 0x3C, 0x00};  // push edi; mov edi,ecx; cmp byte [edi+3Ch],0
using FaceMorphFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 changed);
FaceMorphFn g_faceMorph = nullptr;
std::uint64_t g_packStamp = 0;                             // under g_packLock
std::unordered_map<UInt32, std::uint64_t>* g_dataStamped = nullptr;   // geometry data -> the stamp of its last morph
std::unordered_map<std::uint64_t, std::uint64_t>* g_keyPacked = nullptr;  // (data, decl, stream) -> the stamp of its last pack
// Which bytes of the 88-byte vertex differ between a key's consecutive packs:
// the last staging block per key kept (volatile packs of 309 320 bytes only).
std::unordered_map<std::uint64_t, unsigned char*>* g_keyBytes = nullptr;
std::uint64_t g_offsetDiffers[88] = {};
std::uint64_t g_verticesCompared = 0, g_verticesDiffer = 0;
// The gate's question: a pack whose source arrays did not change - does any
// byte outside the 16 at +40 differ from that key's last pack?
std::uint64_t g_cleanPacksCompared = 0, g_cleanPacksOutsideGap = 0;
UInt32 g_declsDescribed[8] = {};
std::uint64_t g_cleanSame = 0, g_cleanChanged = 0, g_dirtySame = 0, g_dirtyChanged = 0, g_morphCalls = 0;

UInt32 __fastcall ProbeFaceMorph(void* self, void* /*edx*/, UInt32 changed) {
	const std::uintptr_t object = reinterpret_cast<std::uintptr_t>(self);
	AcquireSRWLockExclusive(&g_packLock);
	++g_morphCalls;
	const std::uint64_t stamp = ++g_packStamp;
	(*g_dataStamped)[static_cast<UInt32>(object)] = stamp;
	(*g_dataStamped)[ReadAt<UInt32>(object + 0x34)] = stamp;
	ReleaseSRWLockExclusive(&g_packLock);
	return g_faceMorph(self, nullptr, changed);
}

UInt32 __fastcall ProbePacker(void* self, void* /*edx*/, UInt32 record, UInt32 geometry, UInt32 skin, UInt32 partition, UInt32 decl,
                              UInt32 force) {
	PackContext& context = g_packContexts.Get();
	const unsigned __int64 start = __rdtsc();
	if (context.depth++ == 0) {
		context.geometry = geometry;
		context.partition = partition;
		context.decl = decl;
		context.skin = skin;
		// The original tests the record and the geometry for null before it
		// reads either (JE 0x007633BD at 0x0076313F and 0x0076314B, read):
		// read the consistency word only where it would.
		context.volatileData = false;
		if (record != 0 && geometry != 0) {
			const auto consistency = *reinterpret_cast<volatile UInt16*>(static_cast<std::uintptr_t>(geometry) + kGeometryConsistencyOffset);
			context.volatileData = (consistency & 0xF000u) == 0x8000u;
			context.sourceHash = 0;
			if (context.volatileData && g_dataStamped != nullptr) {
				// NiGeometryData (xOBSE): the vertex count at +0x08, the vertex array at +0x1C, the normals at +0x20.
				const UInt32 count = ReadAt<UInt16>(geometry + 0x08);
				const UInt32 vertices = ReadAt<UInt32>(geometry + 0x1C);
				const UInt32 normals = ReadAt<UInt32>(geometry + 0x20);
				if (vertices != 0 && !IsBadReadPtr(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vertices)), count * 12)) {
					context.sourceHash = probe::HashBytes(reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(vertices)), count * 12);
				}
				if (normals != 0 && !IsBadReadPtr(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(normals)), count * 12)) {
					context.sourceHash ^= probe::HashBytes(reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(normals)), count * 12) * 31u;
				}
			}
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
	// The first volatile packs described once: what is packed, how large.
	static volatile LONG s_described = 0;
	if (context->volatileData && InterlockedIncrement(&s_described) <= 40) {
		const UInt32 data = context->geometry;
		const UInt32 vertices = data != 0 ? ReadAt<UInt16>(data + 0x08) : 0;
		UInt32 bones = 0;
		UInt32 partitionVertices = 0;
		if (context->skin != 0) {
			const UInt32 skinData = ReadAt<UInt32>(context->skin + 0x08);
			bones = skinData != 0 ? ReadAt<UInt32>(skinData + 0x40) : 0;
		}
		if (context->partition != 0) {
			partitionVertices = ReadAt<UInt16>(context->partition + 0x08);  // unverified offset: logged raw for reading
		}
		g_log("Probe: volatile pack - data %08X (vtable %08X) vertices %u, skin %08X bones %u, partition %08X word+8 %u, stream %u, decl %08X: %u bytes "
		      "locked (%.1f a vertex), chip size %u",
		      data, data != 0 ? ReadAt<UInt32>(data) : 0, vertices, context->skin, bones, context->partition, partitionVertices, context->stream,
		      context->decl, bytes, vertices != 0 ? static_cast<double>(bytes) / vertices : 0.0, slice.size);
	}
	AcquireSRWLockExclusive(&g_packLock);
	const probe::PackVerdict verdict =
	    g_packCache->Add(probe::PackKey{context->geometry, context->partition, context->stream, context->decl}, hash, bytes, g_packStats, slice);
	probe::CountVerdictByConsistency(verdict, context->volatileData, g_packStats);
	if (context->volatileData && g_dataStamped != nullptr) {
		const std::uint64_t key = (static_cast<std::uint64_t>(context->geometry) << 32) ^ (static_cast<std::uint64_t>(context->decl) << 4) ^ context->stream;
		// Third test (2026-11-09): "clean" is "the source arrays hash as at this key's last pack".
		const auto packed = g_keyPacked->find(key);
		const bool clean = packed != g_keyPacked->end() && packed->second == context->sourceHash && context->sourceHash != 0;
		if (verdict == probe::PackVerdict::Same) {
			++(clean ? g_cleanSame : g_dirtySame);
		} else if (verdict == probe::PackVerdict::Changed) {
			++(clean ? g_cleanChanged : g_dirtyChanged);
		}
		(*g_keyPacked)[key] = context->sourceHash;
		if (bytes == 309320 && staging != nullptr && g_keyBytes != nullptr) {
			unsigned char*& kept = (*g_keyBytes)[key];
			if (kept == nullptr) {
				kept = static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(), 0, bytes));
			} else if (verdict == probe::PackVerdict::Changed) {
				bool outsideGap = false;
				for (UInt32 v = 0; v < bytes / 88; ++v) {
					bool any = false;
					for (UInt32 o = 0; o < 88; ++o) {
						if (kept[v * 88 + o] != staging[v * 88 + o]) {
							++g_offsetDiffers[o];
							any = true;
							if (o < 40 || o >= 56) {
								outsideGap = true;
							}
						}
					}
					++g_verticesCompared;
					if (any) {
						++g_verticesDiffer;
					}
				}
				if (clean) {
					++g_cleanPacksCompared;
					if (outsideGap) {
						++g_cleanPacksOutsideGap;
					}
				}
			}
			if (kept != nullptr) {
				std::memcpy(kept, staging, bytes);
			}
			// The declaration's entries, once per declaration: offset, selector, type.
			for (UInt32& described : g_declsDescribed) {
				if (described == context->decl) {
					break;
				}
				if (described != 0) {
					continue;
				}
				described = context->decl;
				const UInt32 decl = context->decl;
				const UInt32 entryCount = ReadAt<UInt32>(decl + 0x1C);
				const UInt32 stream = ReadAt<UInt32>(decl + 0x24) + context->stream * 16;
				const UInt32 entries = ReadAt<UInt32>(stream + 0x08);
				char text[700];
				int n = std::snprintf(text, sizeof(text), "Probe: declaration %08X stream %u stride %u, %u entries (offset:selector:type):", decl, context->stream,
				                      ReadAt<UInt32>(stream + 0x0C), entryCount);
				for (UInt32 e = 0; e < entryCount && e < 24 && n < 640 && entries != 0; ++e) {
					n += std::snprintf(text + n, sizeof(text) - n, " %u:%d:%u", ReadAt<UInt32>(entries + e * 0x1C + 0x04),
					                   static_cast<int>(ReadAt<UInt32>(entries + e * 0x1C + 0x08)), ReadAt<UInt32>(entries + e * 0x1C + 0x0C));
				}
				const UInt32 data = context->geometry;
				std::snprintf(text + n, sizeof(text) - n, "; data colours %08X, partition bones a vertex %u, shader flag +0x34 n/a", ReadAt<UInt32>(data + 0x24),
				              context->partition != 0 ? ReadAt<UInt16>(context->partition + 0x24) : 0);
				g_log("%s", text);
				break;
			}
		}
	}
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
	g_dataStamped = new std::unordered_map<UInt32, std::uint64_t>();
	g_keyPacked = new std::unordered_map<std::uint64_t, std::uint64_t>();
	g_keyBytes = new std::unordered_map<std::uint64_t, unsigned char*>();
	if (!InstallEntryDetour("geometry modify bracket 0x00728AB0 (pack cache: the change signal)", kFaceMorph, kFaceMorphBytes, sizeof(kFaceMorphBytes),
	                        reinterpret_cast<const void*>(&ProbeFaceMorph), reinterpret_cast<void**>(&g_faceMorph), log)) {
		delete g_dataStamped;
		g_dataStamped = nullptr;
	}
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
	if (g_dataStamped != nullptr) {
		AcquireSRWLockExclusive(&g_packLock);
		const std::uint64_t cs = g_cleanSame, cc = g_cleanChanged, ds = g_dirtySame, dc = g_dirtyChanged, mc = g_morphCalls;
		g_cleanSame = g_cleanChanged = g_dirtySame = g_dirtyChanged = g_morphCalls = 0;
		ReleaseSRWLockExclusive(&g_packLock);
		g_log("Probe: pack cache change signal (modify bracket 0x00728AB0, %llu calls) - volatile packs with the source arrays as at their last pack: %llu same, %llu "
		      "CHANGED; with changed source arrays: %llu same, %llu changed",
		      mc, cs, cc, ds, dc);
		char offsets[900];
		int at = 0;
		AcquireSRWLockExclusive(&g_packLock);
		at += std::snprintf(offsets, sizeof(offsets), "Probe: pack cache changed packs by vertex byte - %llu of %llu vertices differ; per offset:",
		                    g_verticesDiffer, g_verticesCompared);
		for (unsigned o = 0; o < 88 && at < 850; o += 4) {
			at += std::snprintf(offsets + at, sizeof(offsets) - at, " %u:%llu", o,
			                    g_offsetDiffers[o] + g_offsetDiffers[o + 1] + g_offsetDiffers[o + 2] + g_offsetDiffers[o + 3]);
			g_offsetDiffers[o] = g_offsetDiffers[o + 1] = g_offsetDiffers[o + 2] = g_offsetDiffers[o + 3] = 0;
		}
		g_verticesDiffer = g_verticesCompared = 0;
		ReleaseSRWLockExclusive(&g_packLock);
		g_log("%s", offsets);
		g_log("Probe: pack cache gate - changed packs with an unchanged source: %llu compared, %llu with a difference outside vertex bytes 40..55",
		      g_cleanPacksCompared, g_cleanPacksOutsideGap);
	}
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

// The view-dependence probe ([Probe] ViewConstants): the stereo route's eye
// switch marks the second eye's re-apply (phase 1, SetProbeEyePhase); the
// apply's entries are tabled at ApplyConstants 0x009A8E30's entry and the
// phase-1 uploads are keyed by them (ProbeLogic.h ViewStats).
constexpr UInt32 kEntryLiveOffset = 0x08;
constexpr UInt32 kEntryIdOffset = 0x10;
constexpr UInt32 kMapEntriesOffset = 0x10;
constexpr UInt32 kMapCountOffset = 0x16;
ApplyMapFn g_viewApply = nullptr;
bool g_viewOn = false;
volatile LONG g_eyePhase = 0;
probe::ApplyTable g_applyTable;
probe::ViewStats g_viewStats;

void TableApply(UInt32 map) {
	g_applyTable.Clear();
	const UInt32 entries = ReadAt<UInt32>(map + kMapEntriesOffset);
	const UInt32 count = ReadAt<UInt16>(map + kMapCountOffset);
	if (entries == 0) {
		return;
	}
	for (UInt32 i = 0; i < count; ++i) {
		const UInt32 entry = ReadAt<UInt32>(entries + 4 * i);
		if (entry == 0) {
			continue;
		}
		if (ReadAt<UInt8>(entry + kEntryLiveOffset) == 0) {
			break;  // the original's loop ends here too (0x009A8E30.md B3)
		}
		const UInt32 reg = ReadAt<UInt32>(entry + kEntryRegisterOffset);
		if (reg == 0xFFFFFFFFu) {
			continue;
		}
		const probe::ApplyRow row{reg, ReadAt<UInt32>(entry + kEntryCountOffset),
		                          static_cast<std::uint8_t>(ReadAt<UInt32>(entry + kEntryTypeOffset) >> 28),
		                          static_cast<std::uint16_t>(ReadAt<UInt32>(entry + kEntryIdOffset))};
		g_applyTable.Add(row);
		g_viewStats.Tabled(row);
	}
}

UInt32 __fastcall ViewApplyMap(void* self, void* /*edx*/, UInt32 a1, UInt32 a2, UInt32 a3, UInt32 a4, UInt32 a5, UInt32 a6, UInt32 a7,
                               UInt32 a8, UInt32 a9, UInt32 a10, UInt32 a11) {
	if (g_eyePhase == 1 && self != nullptr) {
		AcquireSRWLockExclusive(&g_uploadLock);
		TableApply(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self)));
		++g_viewStats.applies;
		ReleaseSRWLockExclusive(&g_uploadLock);
	}
	return g_viewApply(self, nullptr, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

// ---- The renderer's other draw entries ([Probe] DrawEntries) ----------------------
//
// NiDX9Renderer (vtable 0x00A88EA4) draws through six entries: Do_RenderShape
// 0x007693E0 (slots 88/89, routed through DrawPrimitive / DrawSkinnedPrimitive)
// and four that go to the device with the pipeline the caller set up: slots 90
// 0x007694F0 and 91 0x00769700 (thiscall(geometry), RET 4), slot 85 0x00766F30
// (EndBatch, by its string; plain RET) and slot 93 0x00766880 (thiscall(one
// argument), RET 4) - read in the vtable and with objdump (2026-11-04). The
// probe counts their calls, to say which of them draws what the stereo route
// misses (the near terrain and the SpeedTree trees of OBVR's water scene).
constexpr UInt32 kDrawSlot90 = 0x007694F0;
constexpr UInt8 kDrawSlot90Bytes[] = {0x83, 0xEC, 0x14, 0x55, 0x8B, 0xE9};  // sub esp,14h; push ebp; mov ebp,ecx
constexpr UInt32 kDrawSlot91 = 0x00769700;
constexpr UInt8 kDrawSlot91Bytes[] = {0x83, 0xEC, 0x24, 0x55, 0x8B, 0xE9};  // sub esp,24h; push ebp; mov ebp,ecx
constexpr UInt32 kEndBatch = 0x00766F30;
constexpr UInt8 kEndBatchBytes[] = {0x83, 0xEC, 0x14, 0x53, 0x56, 0x8B, 0xF1};  // sub esp,14h; push ebx; push esi; mov esi,ecx
constexpr UInt32 kDrawSlot93 = 0x00766880;
constexpr UInt8 kDrawSlot93Bytes[] = {0x83, 0xEC, 0x74, 0x53, 0x8B, 0xD9};  // sub esp,74h; push ebx; mov ebx,ecx
using DrawOneArgFn = UInt32(__fastcall*)(void* self, void* edx, UInt32);
using DrawNoArgFn = UInt32(__fastcall*)(void* self, void* edx);
DrawOneArgFn g_drawSlot90 = nullptr;
DrawOneArgFn g_drawSlot91 = nullptr;
DrawNoArgFn g_endBatch = nullptr;
DrawOneArgFn g_drawSlot93 = nullptr;
bool g_drawEntriesOn = false;
volatile LONG g_drawEntryCalls[4] = {0, 0, 0, 0};

UInt32 __fastcall ProbeDrawSlot90(void* self, void* /*edx*/, UInt32 geometry) {
	InterlockedIncrement(&g_drawEntryCalls[0]);
	return g_drawSlot90(self, nullptr, geometry);
}
UInt32 __fastcall ProbeDrawSlot91(void* self, void* /*edx*/, UInt32 geometry) {
	InterlockedIncrement(&g_drawEntryCalls[1]);
	return g_drawSlot91(self, nullptr, geometry);
}
UInt32 __fastcall ProbeEndBatch(void* self, void* /*edx*/) {
	InterlockedIncrement(&g_drawEntryCalls[2]);
	return g_endBatch(self, nullptr);
}
UInt32 __fastcall ProbeDrawSlot93(void* self, void* /*edx*/, UInt32 a1) {
	InterlockedIncrement(&g_drawEntryCalls[3]);
	return g_drawSlot93(self, nullptr, a1);
}

UInt32 InstallDrawEntries(LogFn log) {
	UInt32 made = 0;
	made += InstallEntryDetour("NiDX9Renderer slot 90 0x007694F0 (draw entries)", kDrawSlot90, kDrawSlot90Bytes, sizeof(kDrawSlot90Bytes),
	                           reinterpret_cast<const void*>(&ProbeDrawSlot90), reinterpret_cast<void**>(&g_drawSlot90), log) ? 1 : 0;
	made += InstallEntryDetour("NiDX9Renderer slot 91 0x00769700 (draw entries)", kDrawSlot91, kDrawSlot91Bytes, sizeof(kDrawSlot91Bytes),
	                           reinterpret_cast<const void*>(&ProbeDrawSlot91), reinterpret_cast<void**>(&g_drawSlot91), log) ? 1 : 0;
	made += InstallEntryDetour("NiDX9Renderer::EndBatch 0x00766F30 (draw entries)", kEndBatch, kEndBatchBytes, sizeof(kEndBatchBytes),
	                           reinterpret_cast<const void*>(&ProbeEndBatch), reinterpret_cast<void**>(&g_endBatch), log) ? 1 : 0;
	made += InstallEntryDetour("NiDX9Renderer slot 93 0x00766880 (draw entries)", kDrawSlot93, kDrawSlot93Bytes, sizeof(kDrawSlot93Bytes),
	                           reinterpret_cast<const void*>(&ProbeDrawSlot93), reinterpret_cast<void**>(&g_drawSlot93), log) ? 1 : 0;
	g_drawEntriesOn = made != 0;
	return made;
}

void LogDrawEntries() {
	const LONG c0 = InterlockedExchange(&g_drawEntryCalls[0], 0);
	const LONG c1 = InterlockedExchange(&g_drawEntryCalls[1], 0);
	const LONG c2 = InterlockedExchange(&g_drawEntryCalls[2], 0);
	const LONG c3 = InterlockedExchange(&g_drawEntryCalls[3], 0);
	g_log("Probe: draw entries - slot 90 0x007694F0 %ld, slot 91 0x00769700 %ld, EndBatch 0x00766F30 %ld, slot 93 0x00766880 %ld", c0, c1, c2, c3);
}

// Under g_uploadLock: one upload's verdict keyed for the view probe.
void NoteViewUpload(UInt32 reg, probe::PackVerdict verdict, UInt32 bytes) {
	if (g_viewOn && g_eyePhase == 1) {
		g_viewStats.Add(g_applyTable.Find(reg), verdict, bytes);
	}
}


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
			NoteViewUpload(reg, g_registers->Add(reg, hash, bytes, static_cast<UInt32>(g_pass), g_uploadStats), bytes);
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
			NoteViewUpload(reg0 + i * stride, g_registers->Add(reg0 + i * stride, probe::HashBytes(rows, bytesPerBone), bytesPerBone,
			                                                   static_cast<UInt32>(g_pass), g_uploadStats), bytesPerBone);
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

// One detour: ApplyConstants 0x009A8E30 (shared with the pass steps, which
// hold it when they are on: then this probe is skipped, said in the log).
UInt32 InstallViewConstants(bool passStepsOn, LogFn log) {
	if (passStepsOn) {
		log("Probe: view constants skipped - the pass steps hold ApplyConstants 0x009A8E30");
		return 0;
	}
	const UInt32 made = InstallEntryDetour("ApplyConstants 0x009A8E30 (view constants)", kApplyMap, kApplyMapBytes, sizeof(kApplyMapBytes),
	                                       reinterpret_cast<const void*>(&ViewApplyMap), reinterpret_cast<void**>(&g_viewApply), log) ? 1 : 0;
	g_viewOn = made == 1 && g_uploadsOn;
	return made;
}

void LogViewConstants() {
	AcquireSRWLockExclusive(&g_uploadLock);
	const probe::ViewStats stats = g_viewStats;
	g_viewStats.Reset();
	ReleaseSRWLockExclusive(&g_uploadLock);
	char line[1500];
	probe::FormatViewStats(stats, line, sizeof(line));
	g_log("%s", line);
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

// ---- The scene graph update probe (knowledge/render-thread-outside-the-pass.md) ----
//
// Eight entry detours on Gamebryo's update tree and its drivers; the two
// drivers are timed (one RDTSC pair a call: one and a few hundred calls a
// frame), the tree's functions only counted (tens of thousands a frame). The
// entries' bytes are the first whole instructions of each function
// (re/export/frame-update/<address>/asm.s); where a CMP precedes a JZ the
// displaced bytes are the CMP and pushes only, and the trampoline's jump
// back lands on the JZ with the flags untouched (PUSH and MOV set none).
constexpr UInt32 kGridUpdate = 0x004823D0;
constexpr UInt8 kGridUpdateBytes[] = {0xD9, 0x44, 0x24, 0x04, 0x53, 0x55, 0x57, 0x51};  // fld [esp+4]; push ebx; push ebp; push edi; push ecx
constexpr UInt32 kCellUpdate = 0x004D4970;
constexpr UInt8 kCellUpdateBytes[] = {0x55, 0x8B, 0xE9, 0x56, 0xC7, 0x05, 0x04, 0x5C, 0xB3, 0x00, 0x00, 0x00, 0x00, 0x00};  // push ebp; mov ebp,ecx; push esi; mov [0xB35C04],0
constexpr UInt32 kNodeDownward = 0x0070A0D0;
constexpr UInt8 kNodeDownwardBytes[] = {0x53, 0x8B, 0x5C, 0x24, 0x0C, 0x84, 0xDB};  // push ebx; mov ebx,[esp+0Ch]; test bl,bl
constexpr UInt32 kNodeSelected = 0x0070A190;
constexpr UInt8 kNodeSelectedBytes[] = {0xD9, 0x44, 0x24, 0x04, 0x53, 0x57, 0x8B, 0xF9};  // fld [esp+4]; push ebx; push edi; mov edi,ecx
constexpr UInt32 kLeafDownward = 0x00707580;
constexpr UInt8 kLeafDownwardBytes[] = {0x80, 0x7C, 0x24, 0x08, 0x00, 0x56, 0x8B, 0xF1};  // cmp byte [esp+8],0; push esi; mov esi,ecx (then jz)
constexpr UInt32 kControllerWalk = 0x0047C930;
constexpr UInt8 kControllerWalkBytes[] = {0x80, 0x7C, 0x24, 0x08, 0x00, 0x56, 0x57, 0x8B, 0xF9};  // cmp byte [esp+8],0; push esi; push edi; mov edi,ecx (then jz)
constexpr UInt32 kWorldData = 0x0070C120;
constexpr UInt8 kWorldDataBytes[] = {0x83, 0xEC, 0x34, 0x53, 0x8B, 0xD9};  // sub esp,34h; push ebx; mov ebx,ecx
constexpr UInt32 kBoundMerge = 0x0072A6B0;
constexpr UInt8 kBoundMergeBytes[] = {0x83, 0xEC, 0x20, 0x56, 0x8B, 0xF1};  // sub esp,20h; push esi; mov esi,ecx
constexpr UInt32 kGeometryBound = 0x00722AA0;
constexpr UInt8 kGeometryBoundBytes[] = {0x83, 0xEC, 0x20, 0x56, 0x8B, 0xF1};  // sub esp,20h; push esi; mov esi,ecx
constexpr UInt32 kUpwardPass = 0x00709EC0;
constexpr UInt8 kUpwardPassBytes[] = {0x8B, 0x01, 0x8B, 0x50, 0x78, 0x56, 0x8B, 0x71, 0x1C};  // mov eax,[ecx]; mov edx,[eax+78h]; push esi; mov esi,[ecx+1Ch]
constexpr UInt32 kObjectUpdate = 0x00707370;
constexpr UInt8 kObjectUpdateBytes[] = {0xD9, 0x44, 0x24, 0x04, 0x56, 0x8B, 0xF1};  // fld [esp+4]; push esi; mov esi,ecx
// NiObjectNET's controllers (+0x0C) and NiAVObject's property list (+0x9C,
// an NiTListBase whose first dword is the head), as 0x0047C930 reads them.
constexpr UInt32 kObjectControllersOffset = 0x0C;
constexpr UInt32 kObjectPropertyListOffset = 0x9C;

// __fastcall with a dummy edx mirrors __thiscall; the floats stay stack
// arguments and are passed through as bits.
using TimeOnlyFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 timeBits);               // RET 4
using TimeFlagFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 timeBits, UInt32 flag);  // RET 8
using NoArgFn = UInt32(__fastcall*)(void* self, void* edx);

TimeOnlyFn g_gridUpdate = nullptr;
TimeOnlyFn g_cellUpdate = nullptr;
TimeFlagFn g_nodeDownward = nullptr;
TimeOnlyFn g_nodeSelected = nullptr;
TimeFlagFn g_leafDownward = nullptr;
TimeFlagFn g_controllerWalk = nullptr;
NoArgFn g_worldData = nullptr;
TimeFlagFn g_objectUpdate = nullptr;
using OneArgFn = UInt32(__fastcall*)(void* self, void* edx, const void* arg);  // RET 4
OneArgFn g_boundMerge = nullptr;
NoArgFn g_geometryBound = nullptr;
NoArgFn g_upwardPass = nullptr;

// Inside GridCellArray::Update (CountGridUpdate) the grid's own steps are
// counted and the top-level selected and upward passes timed, on that
// thread only - the same flag and thread-id scheme as the actor call's.
volatile LONG g_gridActive = 0;
DWORD g_gridThread = 0;
int g_selectedDepth = 0;
int g_upwardDepth = 0;
UInt64 g_gridSelectedCalls = 0;
UInt64 g_gridSelectedTicks = 0;
UInt64 g_gridNodesSelected = 0;
UInt64 g_gridUpwardCalls = 0;
UInt64 g_gridUpwardTicks = 0;
UInt64 g_gridWorldData = 0;
UInt64 g_gridControllerWalks = 0;
UInt64 g_gridMerges = 0;

inline bool InGridUpdate() {
	return g_gridActive != 0 && __readfsdword(0x24) == g_gridThread;
}

struct AtomicGridSplit {
	volatile LONG64 gridUpdates = 0, gridTicks = 0, selectedCalls = 0, selectedTicks = 0, nodesSelected = 0, upwardCalls = 0, upwardTicks = 0,
	                worldData = 0, controllerWalks = 0, merges = 0;
	void Add(const probe::GridSplitStats& s) {
		InterlockedAdd64(&gridUpdates, static_cast<LONG64>(s.gridUpdates));
		InterlockedAdd64(&gridTicks, static_cast<LONG64>(s.gridTicks));
		InterlockedAdd64(&selectedCalls, static_cast<LONG64>(s.selectedCalls));
		InterlockedAdd64(&selectedTicks, static_cast<LONG64>(s.selectedTicks));
		InterlockedAdd64(&nodesSelected, static_cast<LONG64>(s.nodesSelected));
		InterlockedAdd64(&upwardCalls, static_cast<LONG64>(s.upwardCalls));
		InterlockedAdd64(&upwardTicks, static_cast<LONG64>(s.upwardTicks));
		InterlockedAdd64(&worldData, static_cast<LONG64>(s.worldData));
		InterlockedAdd64(&controllerWalks, static_cast<LONG64>(s.controllerWalks));
		InterlockedAdd64(&merges, static_cast<LONG64>(s.merges));
	}
	probe::GridSplitStats TakeSnapshot() {
		probe::GridSplitStats o;
		const auto take = [](volatile LONG64* v) { return static_cast<std::uint64_t>(InterlockedExchange64(v, 0)); };
		o.gridUpdates = take(&gridUpdates);
		o.gridTicks = take(&gridTicks);
		o.selectedCalls = take(&selectedCalls);
		o.selectedTicks = take(&selectedTicks);
		o.nodesSelected = take(&nodesSelected);
		o.upwardCalls = take(&upwardCalls);
		o.upwardTicks = take(&upwardTicks);
		o.worldData = take(&worldData);
		o.controllerWalks = take(&controllerWalks);
		o.merges = take(&merges);
		return o;
	}
};
AtomicGridSplit g_gridSplit;

// Inside one ApplyAnimData call (CountObjectUpdate below) the tree's steps
// are timed as well, on that thread only: the flag and the thread id are set
// around the call, the step sums are the render thread's own until the call
// ends and adds them, as one batch, to the interlocked buckets. The thread
// id comes from the TEB (fs:[24h]) - no thread_local, as RangeWalkGame.h
// says, and no import call per step.
volatile LONG g_actorActive = 0;
DWORD g_actorThread = 0;
UInt64 g_actorWorldDataTicks = 0;
UInt64 g_actorControllerTicks = 0;
UInt64 g_actorMergeTicks = 0;
UInt64 g_actorGeometryBoundTicks = 0;
UInt64 g_actorMerges = 0;
UInt64 g_actorGeometryBounds = 0;
UInt64 g_timedStepOverheadTicks = 0;  // two TSC reads and the arithmetic, measured at install

inline bool InActorUpdate() {
	return g_actorActive != 0 && __readfsdword(0x24) == g_actorThread;
}
bool g_sceneUpdateOn = false;
probe::ClockPair g_sceneUpdateClock;
AtomicTiming g_gridTiming;
AtomicTiming g_objectUpdateTiming;
// The same calls split by their return site: which callers (the actor update
// 0x00471F20 among 145 in the exe) take the time.
AtomicSiteTiming<probe::ObjectUpdateSites::kSites> g_objectUpdateSites;
// The calls from ApplyAnimData 0x00471FBE (one actor each) by the nodes they
// visit: the tree counters' difference across the call, the ticks, in the
// logic's buckets (probe::ActorUpdateStats), interlocked per field.
constexpr UInt32 kApplyAnimDataSite = 0x00471FBE;  // the return into ActorAnimData::ApplyAnimData 0x00471F20
struct AtomicActorUpdate {
	struct Bucket {
		volatile LONG64 calls = 0;
		volatile LONG64 nodes = 0;
		volatile LONG64 worldData = 0;
		volatile LONG64 controllerWalks = 0;
		volatile LONG64 merges = 0;
		volatile LONG64 geometryBounds = 0;
		volatile LONG64 ticks = 0;
		volatile LONG64 worldDataTicks = 0;
		volatile LONG64 controllerTicks = 0;
		volatile LONG64 mergeTicks = 0;
		volatile LONG64 geometryBoundTicks = 0;
	};
	Bucket buckets[probe::ActorUpdateStats::kBuckets];

	void Add(const probe::ActorUpdateParts& p) {
		Bucket& b = buckets[probe::ActorUpdateStats::BucketOf(p.nodes)];
		InterlockedIncrement64(&b.calls);
		InterlockedAdd64(&b.nodes, static_cast<LONG64>(p.nodes));
		InterlockedAdd64(&b.worldData, static_cast<LONG64>(p.worldData));
		InterlockedAdd64(&b.controllerWalks, static_cast<LONG64>(p.controllerWalks));
		InterlockedAdd64(&b.merges, static_cast<LONG64>(p.merges));
		InterlockedAdd64(&b.geometryBounds, static_cast<LONG64>(p.geometryBounds));
		InterlockedAdd64(&b.ticks, static_cast<LONG64>(p.ticks));
		InterlockedAdd64(&b.worldDataTicks, static_cast<LONG64>(p.worldDataTicks));
		InterlockedAdd64(&b.controllerTicks, static_cast<LONG64>(p.controllerTicks));
		InterlockedAdd64(&b.mergeTicks, static_cast<LONG64>(p.mergeTicks));
		InterlockedAdd64(&b.geometryBoundTicks, static_cast<LONG64>(p.geometryBoundTicks));
	}
	probe::ActorUpdateStats TakeSnapshot() {
		probe::ActorUpdateStats out;
		for (unsigned i = 0; i < probe::ActorUpdateStats::kBuckets; ++i) {
			Bucket& b = buckets[i];
			probe::ActorUpdateStats::Bucket& o = out.buckets[i];
			const auto take = [](volatile LONG64* v) { return static_cast<std::uint64_t>(InterlockedExchange64(v, 0)); };
			o.calls = take(&b.calls);
			o.sum.nodes = take(&b.nodes);
			o.sum.worldData = take(&b.worldData);
			o.sum.controllerWalks = take(&b.controllerWalks);
			o.sum.merges = take(&b.merges);
			o.sum.geometryBounds = take(&b.geometryBounds);
			o.sum.ticks = take(&b.ticks);
			o.sum.worldDataTicks = take(&b.worldDataTicks);
			o.sum.controllerTicks = take(&b.controllerTicks);
			o.sum.mergeTicks = take(&b.mergeTicks);
			o.sum.geometryBoundTicks = take(&b.geometryBoundTicks);
		}
		return out;
	}
};
AtomicActorUpdate g_actorUpdate;
volatile LONG64 g_sceneCells = 0;
volatile LONG64 g_sceneNodes = 0;
volatile LONG64 g_sceneSelected = 0;
volatile LONG64 g_sceneLeaves = 0;
volatile LONG64 g_sceneWalks = 0;
volatile LONG64 g_sceneWalksIdle = 0;
volatile LONG64 g_sceneWorldData = 0;

UInt32 __fastcall CountGridUpdate(void* self, void* /*edx*/, UInt32 timeBits) {
	g_gridSelectedCalls = g_gridSelectedTicks = g_gridNodesSelected = g_gridUpwardCalls = g_gridUpwardTicks = 0;
	g_gridWorldData = g_gridControllerWalks = g_gridMerges = 0;
	g_selectedDepth = g_upwardDepth = 0;
	g_gridThread = __readfsdword(0x24);
	g_gridActive = 1;
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_gridUpdate(self, nullptr, timeBits);
	const unsigned __int64 elapsed = __rdtsc() - start;
	g_gridActive = 0;
	g_gridTiming.Add(elapsed);
	probe::GridSplitStats s;
	s.gridUpdates = 1;
	s.gridTicks = elapsed;
	s.selectedCalls = g_gridSelectedCalls;
	s.selectedTicks = g_gridSelectedTicks;
	s.nodesSelected = g_gridNodesSelected;
	s.upwardCalls = g_gridUpwardCalls;
	s.upwardTicks = g_gridUpwardTicks;
	s.worldData = g_gridWorldData;
	s.controllerWalks = g_gridControllerWalks;
	s.merges = g_gridMerges;
	g_gridSplit.Add(s);
	return eax;
}
UInt32 __fastcall CountUpwardPass(void* self, void* /*edx*/) {
	if (!InGridUpdate()) {
		return g_upwardPass(self, nullptr);
	}
	if (g_upwardDepth++ != 0) {
		const UInt32 eax = g_upwardPass(self, nullptr);
		--g_upwardDepth;
		return eax;
	}
	++g_gridUpwardCalls;
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_upwardPass(self, nullptr);
	g_gridUpwardTicks += __rdtsc() - start;
	--g_upwardDepth;
	return eax;
}
UInt32 __fastcall CountCellUpdate(void* self, void* /*edx*/, UInt32 timeBits) {
	InterlockedIncrement64(&g_sceneCells);
	return g_cellUpdate(self, nullptr, timeBits);
}
UInt32 __fastcall CountNodeDownward(void* self, void* /*edx*/, UInt32 timeBits, UInt32 flag) {
	InterlockedIncrement64(&g_sceneNodes);
	return g_nodeDownward(self, nullptr, timeBits, flag);
}
UInt32 __fastcall CountNodeSelected(void* self, void* /*edx*/, UInt32 timeBits) {
	InterlockedIncrement64(&g_sceneSelected);
	if (!InGridUpdate()) {
		return g_nodeSelected(self, nullptr, timeBits);
	}
	++g_gridNodesSelected;
	if (g_selectedDepth++ != 0) {
		const UInt32 eax = g_nodeSelected(self, nullptr, timeBits);
		--g_selectedDepth;
		return eax;
	}
	++g_gridSelectedCalls;
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_nodeSelected(self, nullptr, timeBits);
	g_gridSelectedTicks += __rdtsc() - start;
	--g_selectedDepth;
	return eax;
}
UInt32 __fastcall CountLeafDownward(void* self, void* /*edx*/, UInt32 timeBits, UInt32 flag) {
	InterlockedIncrement64(&g_sceneLeaves);
	return g_leafDownward(self, nullptr, timeBits, flag);
}
UInt32 __fastcall CountControllerWalk(void* self, void* /*edx*/, UInt32 timeBits, UInt32 flag) {
	InterlockedIncrement64(&g_sceneWalks);
	const std::uintptr_t object = reinterpret_cast<std::uintptr_t>(self);
	const bool properties = (flag & 0xFF) != 0 && *reinterpret_cast<volatile const UInt32*>(object + kObjectPropertyListOffset) != 0;
	const bool controllers = *reinterpret_cast<volatile const UInt32*>(object + kObjectControllersOffset) != 0;
	if (!properties && !controllers) {
		InterlockedIncrement64(&g_sceneWalksIdle);
	}
	if (InGridUpdate()) {
		++g_gridControllerWalks;
	}
	if (!InActorUpdate()) {
		return g_controllerWalk(self, nullptr, timeBits, flag);
	}
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_controllerWalk(self, nullptr, timeBits, flag);
	g_actorControllerTicks += __rdtsc() - start;
	return eax;
}
UInt32 __fastcall CountWorldData(void* self, void* /*edx*/) {
	InterlockedIncrement64(&g_sceneWorldData);
	if (InGridUpdate()) {
		++g_gridWorldData;
	}
	if (!InActorUpdate()) {
		return g_worldData(self, nullptr);
	}
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_worldData(self, nullptr);
	g_actorWorldDataTicks += __rdtsc() - start;
	return eax;
}
UInt32 __fastcall CountBoundMerge(void* self, void* /*edx*/, const void* other) {
	if (InGridUpdate()) {
		++g_gridMerges;
	}
	if (!InActorUpdate()) {
		return g_boundMerge(self, nullptr, other);
	}
	++g_actorMerges;
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_boundMerge(self, nullptr, other);
	g_actorMergeTicks += __rdtsc() - start;
	return eax;
}
UInt32 __fastcall CountGeometryBound(void* self, void* /*edx*/) {
	if (!InActorUpdate()) {
		return g_geometryBound(self, nullptr);
	}
	++g_actorGeometryBounds;
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_geometryBound(self, nullptr);
	g_actorGeometryBoundTicks += __rdtsc() - start;
	return eax;
}
UInt32 __fastcall CountObjectUpdate(void* self, void* /*edx*/, UInt32 timeBits, UInt32 flag) {
	const UInt32 site = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
	if (site != kApplyAnimDataSite) {
		const unsigned __int64 start = __rdtsc();
		const UInt32 eax = g_objectUpdate(self, nullptr, timeBits, flag);
		const unsigned __int64 elapsed = __rdtsc() - start;
		g_objectUpdateTiming.Add(elapsed);
		g_objectUpdateSites.Add(site, elapsed);
		return eax;
	}
	// One actor's skeleton: the tree counters' difference across the call
	// and the steps timed inside it (the loader thread's visits would add to
	// the counters: a small noise, accepted; a difference that ran backwards
	// across the summary's reset is dropped, not binned).
	const LONG64 nodesBefore = g_sceneNodes;
	const LONG64 worldBefore = g_sceneWorldData;
	const LONG64 walksBefore = g_sceneWalks;
	g_actorWorldDataTicks = g_actorControllerTicks = g_actorMergeTicks = g_actorGeometryBoundTicks = 0;
	g_actorMerges = g_actorGeometryBounds = 0;
	g_actorThread = __readfsdword(0x24);
	g_actorActive = 1;
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = g_objectUpdate(self, nullptr, timeBits, flag);
	const unsigned __int64 elapsed = __rdtsc() - start;
	g_actorActive = 0;
	g_objectUpdateTiming.Add(elapsed);
	g_objectUpdateSites.Add(site, elapsed);
	const LONG64 nodesAfter = g_sceneNodes;
	const LONG64 worldAfter = g_sceneWorldData;
	const LONG64 walksAfter = g_sceneWalks;
	if (nodesAfter >= nodesBefore && worldAfter >= worldBefore && walksAfter >= walksBefore) {
		probe::ActorUpdateParts p;
		p.nodes = static_cast<UInt64>(nodesAfter - nodesBefore);
		p.worldData = static_cast<UInt64>(worldAfter - worldBefore);
		p.controllerWalks = static_cast<UInt64>(walksAfter - walksBefore);
		p.merges = g_actorMerges;
		p.geometryBounds = g_actorGeometryBounds;
		p.ticks = elapsed;
		p.worldDataTicks = g_actorWorldDataTicks;
		p.controllerTicks = g_actorControllerTicks;
		p.mergeTicks = g_actorMergeTicks;
		p.geometryBoundTicks = g_actorGeometryBoundTicks;
		g_actorUpdate.Add(p);
	}
	return eax;
}

UInt32 InstallSceneUpdateProbe(LogFn log) {
	UInt32 made = 0;
	made += InstallEntryDetour("GridCellArray::Update 0x004823D0 (scene update)", kGridUpdate, kGridUpdateBytes, sizeof(kGridUpdateBytes),
	                           reinterpret_cast<const void*>(&CountGridUpdate), reinterpret_cast<void**>(&g_gridUpdate), log) ? 1 : 0;
	made += InstallEntryDetour("TESObjectCELL::UpdateSelectedNodes 0x004D4970 (scene update)", kCellUpdate, kCellUpdateBytes, sizeof(kCellUpdateBytes),
	                           reinterpret_cast<const void*>(&CountCellUpdate), reinterpret_cast<void**>(&g_cellUpdate), log) ? 1 : 0;
	made += InstallEntryDetour("NiNode::UpdateDownwardPass 0x0070A0D0 (scene update)", kNodeDownward, kNodeDownwardBytes, sizeof(kNodeDownwardBytes),
	                           reinterpret_cast<const void*>(&CountNodeDownward), reinterpret_cast<void**>(&g_nodeDownward), log) ? 1 : 0;
	made += InstallEntryDetour("NiNode::UpdateSelectedDownwardPass 0x0070A190 (scene update)", kNodeSelected, kNodeSelectedBytes,
	                           sizeof(kNodeSelectedBytes), reinterpret_cast<const void*>(&CountNodeSelected), reinterpret_cast<void**>(&g_nodeSelected),
	                           log) ? 1 : 0;
	made += InstallEntryDetour("NiAVObject::UpdateDownwardPass 0x00707580 (scene update)", kLeafDownward, kLeafDownwardBytes, sizeof(kLeafDownwardBytes),
	                           reinterpret_cast<const void*>(&CountLeafDownward), reinterpret_cast<void**>(&g_leafDownward), log) ? 1 : 0;
	made += InstallEntryDetour("NiAVObject::UpdateObjectControllers 0x0047C930 (scene update)", kControllerWalk, kControllerWalkBytes,
	                           sizeof(kControllerWalkBytes), reinterpret_cast<const void*>(&CountControllerWalk),
	                           reinterpret_cast<void**>(&g_controllerWalk), log) ? 1 : 0;
	made += InstallEntryDetour("NiAVObject::UpdateWorldData 0x0070C120 (scene update)", kWorldData, kWorldDataBytes, sizeof(kWorldDataBytes),
	                           reinterpret_cast<const void*>(&CountWorldData), reinterpret_cast<void**>(&g_worldData), log) ? 1 : 0;
	made += InstallEntryDetour("NiAVObject::Update 0x00707370 (scene update)", kObjectUpdate, kObjectUpdateBytes, sizeof(kObjectUpdateBytes),
	                           reinterpret_cast<const void*>(&CountObjectUpdate), reinterpret_cast<void**>(&g_objectUpdate), log) ? 1 : 0;
	made += InstallEntryDetour("NiBound::Merge 0x0072A6B0 (scene update)", kBoundMerge, kBoundMergeBytes, sizeof(kBoundMergeBytes),
	                           reinterpret_cast<const void*>(&CountBoundMerge), reinterpret_cast<void**>(&g_boundMerge), log) ? 1 : 0;
	made += InstallEntryDetour("NiGeometry::UpdateWorldBound 0x00722AA0 (scene update)", kGeometryBound, kGeometryBoundBytes,
	                           sizeof(kGeometryBoundBytes), reinterpret_cast<const void*>(&CountGeometryBound),
	                           reinterpret_cast<void**>(&g_geometryBound), log) ? 1 : 0;
	made += InstallEntryDetour("NiNode::UpdateUpwardPass 0x00709EC0 (scene update)", kUpwardPass, kUpwardPassBytes, sizeof(kUpwardPassBytes),
	                           reinterpret_cast<const void*>(&CountUpwardPass), reinterpret_cast<void**>(&g_upwardPass), log) ? 1 : 0;
	// The probe's own cost per timed step, for reading the walk: the median
	// of a thousand empty timings.
	{
		unsigned __int64 samples[1001];
		for (unsigned i = 0; i < 1001; ++i) {
			const unsigned __int64 a = __rdtsc();
			const unsigned __int64 b = __rdtsc();
			samples[i] = b - a;
		}
		for (unsigned i = 1; i < 1001; ++i) {  // insertion sort, once
			const unsigned __int64 v = samples[i];
			unsigned j = i;
			while (j > 0 && samples[j - 1] > v) {
				samples[j] = samples[j - 1];
				--j;
			}
			samples[j] = v;
		}
		g_timedStepOverheadTicks = samples[500];
		log("Probe: scene update - a timed step inside ApplyAnimData carries about %llu ticks of the probe's own two TSC reads (median of 1000)",
		    static_cast<unsigned long long>(g_timedStepOverheadTicks));
	}
	g_sceneUpdateOn = made != 0;
	g_sceneUpdateClock = ReadClocks();
	return made;
}

void LogSceneUpdate() {
	const probe::ClockPair now = ReadClocks();
	const double ticksPerMs = probe::TicksPerMillisecond(g_sceneUpdateClock, now, ClockFrequency());
	g_sceneUpdateClock = now;
	probe::SceneUpdateStats s;
	const probe::StepTiming grid = g_gridTiming.TakeSnapshot();
	const probe::StepTiming object = g_objectUpdateTiming.TakeSnapshot();
	s.gridUpdates = grid.calls;
	s.gridTicks = grid.ticks;
	s.objectUpdates = object.calls;
	s.objectUpdateTicks = object.ticks;
	s.cells = static_cast<std::uint64_t>(InterlockedExchange64(&g_sceneCells, 0));
	s.nodeDownward = static_cast<std::uint64_t>(InterlockedExchange64(&g_sceneNodes, 0));
	s.nodeSelected = static_cast<std::uint64_t>(InterlockedExchange64(&g_sceneSelected, 0));
	s.leafDownward = static_cast<std::uint64_t>(InterlockedExchange64(&g_sceneLeaves, 0));
	s.controllerWalks = static_cast<std::uint64_t>(InterlockedExchange64(&g_sceneWalks, 0));
	s.controllerWalksIdle = static_cast<std::uint64_t>(InterlockedExchange64(&g_sceneWalksIdle, 0));
	s.worldData = static_cast<std::uint64_t>(InterlockedExchange64(&g_sceneWorldData, 0));
	char line[6400];  // 160 sites at about 34 characters each
	probe::FormatSceneUpdate(s, ticksPerMs, line, sizeof(line));
	g_log("%s", line);
	const int at = std::snprintf(line, sizeof(line), "Probe: scene update -");
	probe::FormatStepSites("NiAVObject::Update", g_objectUpdateSites.TakeSnapshot(), ticksPerMs, line + at, sizeof(line) - at);
	g_log("%s", line);
	probe::FormatActorUpdate(g_actorUpdate.TakeSnapshot(), ticksPerMs, line, sizeof(line));
	g_log("%s", line);
	probe::FormatGridSplit(g_gridSplit.TakeSnapshot(), ticksPerMs, line, sizeof(line));
	g_log("%s", line);
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

// MemoryHeap, the engine's allocator object (read: the this of
// MemoryHeap::Allocate 0x00401AA0 at its callers, e.g. MOV ECX,0x00B02020
// in the wrapper 0x00401F00; CrashLogger names the same address
// "Class: MemoryHeap" in its stack annotations).
constexpr UInt32 kGameHeap = 0x00B02020;

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

void SetProbeEyePhase(UInt8 phase) {
	InterlockedExchange(&g_eyePhase, phase);
}

UInt32 ProbesAsked(const ShadowSettings& settings) {
	return (settings.callCounters ? static_cast<UInt32>(Probed::Count) : 0u) +
	       (settings.passSteps ? static_cast<UInt32>(Step::Count) - (settings.callCounters ? 1u : 0u) : 0u) +
	       (settings.packCache ? 3u : 0u) + (settings.constantUploads ? (settings.passSteps ? 2u : 3u) : 0u) +
	       (settings.viewConstants && !settings.passSteps ? 1u : 0u) + (settings.drawEntries ? 4u : 0u) + (settings.occlusion ? 4u : 0u) + (settings.unroutedDraws ? 1u : 0u);
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
	if (settings.viewConstants) {
		made += InstallViewConstants(settings.passSteps, log);
	}
	if (settings.sceneUpdate) {
		made += InstallSceneUpdateProbe(log);
	}
	if (settings.drawEntries) {
		made += InstallDrawEntries(log);
	}
	if (settings.occlusion) {
		made += InstallOcclusionProbe(settings.occlusionEvery, log);
	}
	if (settings.grass) {
		InstallGrassProbe(log);  // no detour of its own: it counts through the stereo route's
	}
	if (settings.unroutedDraws) {
		made += InstallUnroutedDrawProbe(log);  // one entry detour; the device's vtable is hooked at the first routed draw
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
	if (g_sceneUpdateOn) {
		LogSceneUpdate();
	}
	if (g_packCacheOn) {
		LogPackCache();
	}
	if (g_uploadsOn) {
		LogConstantUploads();
	}
	if (g_viewOn) {
		LogViewConstants();
	}
	if (g_drawEntriesOn) {
		LogDrawEntries();
	}
	LogOcclusionSummary();
	LogUnroutedDrawSummary();
	if (g_memory) {
		probe::MemorySample sample;
		if (SampleMemory(&sample)) {
			g_memoryPeak.Add(sample);
			probe::FormatMemory(sample, g_memoryPeak, line, sizeof(line));
			g_log("%s", line);
		}
		// The engine's own heap (ProbeLogic.h has the fields as read).
		const UInt8* heap = reinterpret_cast<const UInt8*>(kGameHeap);
		probe::GameHeapSample game;
		std::memcpy(&game.sizeBytes, heap + 0x0C, 4);
		std::memcpy(&game.topBytes, heap + 0x10, 4);
		std::memcpy(&game.highestTopBytes, heap + 0x14, 4);
		std::memcpy(&game.blocks, heap + 0x1C, 4);
		probe::FormatGameHeap(game, line, sizeof(line));
		g_log("%s", line);
	}
}

}  // namespace obng::plugin
