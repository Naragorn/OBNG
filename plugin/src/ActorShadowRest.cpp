#include "ActorShadowRest.h"

#include <windows.h>

#include <intrin.h>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "Memory.h"
#include "engine/render/ActorShadowRestLogic.h"

namespace obng::plugin {

namespace {

namespace rest = obng::engine::render::actor_shadow_rest;

// The sites, read 2026-11-12 in the instructions (re/export/perf-levers/0x004073D0,
// re/export/scene-pass/0x0040C830, re/export/actor-shadow-rest/; knowledge/actor-shadows.md).
constexpr UInt32 kStepCall = 0x0040C91B;       // the scene render's call of the step (ECX = its EDI, unused)
constexpr UInt32 kStep = 0x004073D0;           // the actor shadow step: no argument, plain RET
constexpr UInt32 kResetSite = 0x007C5C61;      // the lights' reset 0x007C5BE0: the map's release begins (EDI = the light)
constexpr UInt8 kResetBytes[] = {0x8B, 0x8F, 0x14, 0x01, 0x00, 0x00};  // mov ecx, [edi+0x114]
constexpr UInt32 kResetRelease = 0x007C5C67;   // the release's next instruction
constexpr UInt32 kResetHide = 0x007C5CA3;      // past the release: the light hidden, 0x007D5320
constexpr UInt32 kDrawSite = 0x004078DA;       // the walk's draw block begins (ESI = the light)
constexpr UInt8 kDrawBytes[] = {0x8B, 0x86, 0x30, 0x01, 0x00, 0x00};  // mov eax, [esi+0x130]
constexpr UInt32 kDrawPlace = 0x004078E0;      // the placement 0x007D2280 follows
constexpr UInt32 kDrawNext = 0x004079BC;       // the walk's next light; [esp+0x1C] counts the lights drawn
constexpr UInt32 kMapsCall = 0x00407A4B;       // the step's call of the maps' render
constexpr UInt32 kMaps = 0x007C6770;           // thiscall(shadow scene; camera), RET 4
constexpr UInt32 kLightMapCall = 0x007C6943;   // the maps' render's call of one light's map
constexpr UInt32 kLightMap = 0x007D59C0;       // thiscall(light; accumulator, camera), RET 8
constexpr UInt32 kPoolRelease = 0x007C1A30;    // thiscall(pool; map), RET 4
constexpr UInt32 kPool = 0x00B42F50;           // the maps' pool, as 0x007C5BE0, 0x007D6390 and 0x007D46C0 pass it
constexpr UInt32 kFadeTime = 0x00B2C680;
constexpr UInt32 kTes = 0x00B333A0;            // its +0x34 picks the map count
constexpr UInt32 kTesCountPick = 0x34;
constexpr UInt32 kCountPicked = 0x00B06F04;    // iActorShadowCountInt, taken with [g_TES]+0x34 set
constexpr UInt32 kCountOther = 0x00B06EFC;     // iActorShadowCountExt
// The shadow light's fields (0x004073D0, 0x007D6390, 0x007D46C0, 0x007C6C30).
constexpr UInt32 kLightFade = 0xD8;
constexpr UInt32 kLightFadeTarget = 0xDC;
constexpr UInt32 kLightFadeTimer = 0xE0;
constexpr UInt32 kLightKind = 0xF5;            // 0 for the casters' lights (0x007C6C30): drawn by 0x007D46C0
constexpr UInt32 kLightSource = 0x100;         // its NiLight; app-culled = hidden
constexpr UInt32 kLightMapHeld = 0x114;
constexpr UInt32 kLightInView = 0x118;         // 0xFF: out of the camera's view
constexpr UInt32 kLightCaster = 0x130;
constexpr UInt32 kNodeFlags = 0x18;            // bit 0: app-culled
constexpr UInt32 kNodeBound = 0x20;            // the world bound's centre, then its radius
constexpr double kMaxGapMs = 100.0;

using StepFn = void(__fastcall*)(UInt32 self, UInt32 edx);
using MapsFn = void(__fastcall*)(UInt32 scene, UInt32 edx, UInt32 camera);
using LightMapFn = void(__fastcall*)(UInt32 light, UInt32 edx, UInt32 accumulator, UInt32 camera);
using PoolReleaseFn = void(__fastcall*)(UInt32 pool, UInt32 edx, UInt32 map);
using DeleteFn = void(__fastcall*)(UInt32 self, UInt32 edx, UInt32 flags);

LogFn g_log = nullptr;
bool g_installed = false;
std::unique_ptr<rest::Table> g_table;
bool g_inStep = false;
UInt32 g_step = 0;
UInt32 g_lastCount = 0xFFFFFFFFu;
LARGE_INTEGER g_lastStepQpc{};
LARGE_INTEGER g_qpcFrequency{};
std::uint64_t g_shownUnrested = 0;  // kept, not at rest, shown at the maps' render: drawn into the map it holds
unsigned __int64 g_summaryTsc = 0;
LARGE_INTEGER g_summaryQpc{};
volatile LONG g_summaryRequested = 0;  // set by the summary timer's thread, written out by the next step

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }
float Float(UInt32 address) { return *reinterpret_cast<const float*>(address); }

UInt32 MapCount() {
	const UInt32 tes = Dword(kTes);
	if (tes == 0) {
		return 0;
	}
	return Dword(tes + kTesCountPick) != 0 ? Dword(kCountPicked) : Dword(kCountOther);
}

rest::LightView ReadLight(UInt32 light) {
	rest::LightView v;
	v.light = light;
	v.caster = Dword(light + kLightCaster);
	if (v.caster != 0) {
		v.center[0] = Float(v.caster + kNodeBound);
		v.center[1] = Float(v.caster + kNodeBound + 4);
		v.center[2] = Float(v.caster + kNodeBound + 8);
	}
	v.map = Dword(light + kLightMapHeld);
	v.visible = *reinterpret_cast<const UInt16*>(light + kLightInView) != 0xFF;
	v.fade = Float(light + kLightFade);
	v.fadeTarget = Float(light + kLightFadeTarget);
	v.fadeTimer = Float(light + kLightFadeTimer);
	v.fadeTime = Float(kFadeTime);
	v.actorKind = *reinterpret_cast<const UInt8*>(light + kLightKind) == 0;
	return v;
}

// What the reset 0x007C5C61..0x007C5C99 does with a light's map, for a light
// kept through it that did not come to rest: back to the pool, the light's
// own reference dropped, the field cleared.
void ReleaseMap(UInt32 light) {
	reinterpret_cast<PoolReleaseFn>(kPoolRelease)(Dword(kPool), 0, Dword(light + kLightMapHeld));
	const UInt32 map = Dword(light + kLightMapHeld);
	if (map == 0) {
		return;
	}
	if (InterlockedDecrement(reinterpret_cast<volatile LONG*>(map + 4)) == 0) {
		reinterpret_cast<DeleteFn>(Dword(Dword(map)))(map, 0, 1);  // vtable slot 0: the deleting destructor
	}
	*reinterpret_cast<UInt32*>(light + kLightMapHeld) = 0;
}

// The window's line, written on the step's thread (the counts are its own).
void WriteSummary() {
	const unsigned __int64 tsc = __rdtsc();
	LARGE_INTEGER qpc{};
	QueryPerformanceCounter(&qpc);
	const double seconds = static_cast<double>(qpc.QuadPart - g_summaryQpc.QuadPart) / static_cast<double>(g_qpcFrequency.QuadPart);
	const double ticksPerMs = seconds > 0.0 ? static_cast<double>(tsc - g_summaryTsc) / (seconds * 1000.0) : 0.0;
	g_summaryTsc = tsc;
	g_summaryQpc = qpc;
	char line[1024];
	rest::FormatSummary(g_table->counts, g_table->settings(), ticksPerMs, line, sizeof(line));
	g_log("%s kept, not at rest and shown at the maps' render %llu", line, static_cast<unsigned long long>(g_shownUnrested));
	g_table->ResetCounts();
	g_shownUnrested = 0;
}

// ---- The five sites ----------------------------------------------------------

// The scene render's call of the step: numbers the step, decides its side
// and its safety, times it.
void __fastcall Step(UInt32 self, UInt32) {
	++g_step;
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);
	const double gapMs = g_lastStepQpc.QuadPart != 0 && g_qpcFrequency.QuadPart != 0
	                         ? static_cast<double>(now.QuadPart - g_lastStepQpc.QuadPart) * 1000.0 / static_cast<double>(g_qpcFrequency.QuadPart)
	                         : -1.0;
	const UInt32 count = MapCount();
	const bool safe = rest::SafeStep(count, g_lastCount, gapMs, kMaxGapMs);
	g_lastCount = count;
	g_lastStepQpc = now;
	const bool on = rest::OnInStep(g_step, g_table->settings().every, g_table->settings().alternate);
	g_table->BeginStep(g_step, on, safe);
	g_inStep = true;
	const unsigned __int64 t0 = __rdtsc();
	reinterpret_cast<StepFn>(kStep)(self, 0);
	g_table->AddStepTicks(on, __rdtsc() - t0);
	g_inStep = false;
	if (InterlockedExchange(&g_summaryRequested, 0) != 0) {
		WriteSummary();
	}
}

// The reset, for a light with its NiLight: 1 = its map stays with it.
UInt32 __stdcall KeepAtReset(UInt32 light) {
	return g_inStep && g_table->Keep(ReadLight(light)) == rest::Why::kKept ? 1u : 0u;
}

// The draw block, for a shown light under the count with its caster shown:
// 1 = at rest (counted as drawn, not placed, nothing collected).
UInt32 __stdcall RestAtDraw(UInt32 light) {
	return g_inStep && g_table->Rest(light, Dword(light + kLightCaster), Float(light + kLightFadeTarget)) ? 1u : 0u;
}

UInt32 g_resetRelease = kResetRelease;
UInt32 g_resetHide = kResetHide;
UInt32 g_drawPlace = kDrawPlace;
UInt32 g_drawNext = kDrawNext;

// The reset's patch: the displaced load, or past the release when kept.
__declspec(naked) void ResetSite() {
	__asm {
		push eax
		push ecx
		push edx
		push edi
		call KeepAtReset
		test eax, eax
		pop edx
		pop ecx
		pop eax
		jnz keeps
		mov ecx, dword ptr [edi + 0x114]
		jmp dword ptr [g_resetRelease]
	keeps:
		jmp dword ptr [g_resetHide]
	}
}

// The draw block's patch: the displaced load, or the light counted and the
// next one taken. EAX, ECX and EDX are written before they are read on both
// ways (0x004078E0, 0x004079BC).
__declspec(naked) void DrawSite() {
	__asm {
		push esi
		call RestAtDraw
		test eax, eax
		jnz rests
		mov eax, dword ptr [esi + 0x130]
		jmp dword ptr [g_drawPlace]
	rests:
		add dword ptr [esp + 0x1C], 1
		jmp dword ptr [g_drawNext]
	}
}

// The step's call of the maps' render: first the maps of the lights kept
// through the reset that did not come to rest go back to the pool (the
// render takes maps from it for the lights drawn now).
void __fastcall Maps(UInt32 scene, UInt32, UInt32 camera) {
	if (g_inStep) {
		UInt32 lights[rest::Table::kSlots] = {};
		const UInt32 n = g_table->Unrested(lights, rest::Table::kSlots);
		for (UInt32 i = 0; i < n; ++i) {
			const UInt32 source = Dword(lights[i] + kLightSource);
			if (source == 0 || (*reinterpret_cast<const UInt16*>(source + kNodeFlags) & 1u) == 0) {
				++g_shownUnrested;  // shown (fading out): its map render below draws into the map it holds
				continue;
			}
			ReleaseMap(lights[i]);
			g_table->Released(lights[i]);
		}
	}
	reinterpret_cast<MapsFn>(kMaps)(scene, 0, camera);
}

// The maps' render's call of one light's map: left out for a light at rest;
// a light drawn now is noted with where its caster stood.
void __fastcall LightMap(UInt32 light, UInt32, UInt32 accumulator, UInt32 camera) {
	if (g_inStep && g_table->Rested(light)) {
		return;
	}
	reinterpret_cast<LightMapFn>(kLightMap)(light, 0, accumulator, camera);
	if (g_inStep) {
		g_table->Drawn(ReadLight(light));
	}
}

// ---- Installing -------------------------------------------------------------

struct Site {
	const char* what;
	UInt32 address;
	UInt8 expected[6];
	UInt32 length;
	UInt8 opcode;  // 0xE8 call (5 bytes), 0xE9 jump (padded with nops)
	const void* to;
};

Site CallSite(const char* what, UInt32 address, UInt32 target, const void* to) {
	Site s{what, address, {0xE8}, 5, 0xE8, to};
	const UInt32 relative = target - (address + 5);
	std::memcpy(&s.expected[1], &relative, sizeof(relative));
	return s;
}

Site JumpSite(const char* what, UInt32 address, const UInt8 (&bytes)[6], const void* to) {
	Site s{what, address, {}, 6, 0xE9, to};
	std::memcpy(s.expected, bytes, sizeof(bytes));
	return s;
}

bool Write(const Site& s) {
	UInt8 patch[6] = {s.opcode, 0, 0, 0, 0, 0x90};
	const UInt32 relative = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(s.to)) - (s.address + 5);
	std::memcpy(&patch[1], &relative, sizeof(relative));
	return SafeWrite(s.address, patch, s.length);
}

}  // namespace

void InstallActorShadowRest(const char* iniPath, LogFn log) {
	if (g_installed) {
		return;
	}
	const UINT every = GetPrivateProfileIntA("Route", "ActorShadowEvery", 1, iniPath);
	if (every <= 1) {
		return;
	}
	g_log = log;
	char still[32] = {};
	GetPrivateProfileStringA("Route", "ActorShadowStill", "4", still, sizeof(still), iniPath);
	rest::Settings settings;
	settings.every = every > 16 ? 16 : every;
	const float units = std::strtof(still, nullptr);
	settings.still = units >= 0.0f && units <= 1000.0f ? units : 4.0f;
	settings.alternate = GetPrivateProfileIntA("Route", "ActorShadowAlternate", 0, iniPath) != 0;
	// Written in this order: the step's call last, since every other site does
	// nothing until it numbers a step - a write that fails leaves the step as
	// the game has it.
	const Site sites[] = {
		CallSite("the maps' render's call of a light's map", kLightMapCall, kLightMap, reinterpret_cast<const void*>(&LightMap)),
		CallSite("the step's call of the maps' render", kMapsCall, kMaps, reinterpret_cast<const void*>(&Maps)),
		JumpSite("the light walk's draw block", kDrawSite, kDrawBytes, reinterpret_cast<const void*>(&DrawSite)),
		JumpSite("the map's release in the lights' reset", kResetSite, kResetBytes, reinterpret_cast<const void*>(&ResetSite)),
		CallSite("the scene render's call of the actor shadow step", kStepCall, kStep, reinterpret_cast<const void*>(&Step)),
	};
	for (const Site& s : sites) {
		if (!Verify(s.address, s.expected, s.length)) {
			char have[32];
			DescribeBytes(s.address, have, sizeof(have));
			log("Route: actor shadows at rest - %s at %08X reads %s, not the game's own bytes; nothing installed", s.what, s.address, have);
			return;
		}
	}
	g_table = std::make_unique<rest::Table>(settings);
	QueryPerformanceFrequency(&g_qpcFrequency);
	for (const Site& s : sites) {
		if (!Write(s)) {
			log("Route: actor shadows at rest - %s at %08X could not be written; the step's call is not, so the sites written before do nothing",
			    s.what, s.address);
			return;
		}
	}
	g_installed = true;
	g_summaryTsc = __rdtsc();
	QueryPerformanceCounter(&g_summaryQpc);
	log("Route: actor shadows at rest - a light whose caster stands within %.1f units of its last draw keeps its map, drawn again in one step "
	    "of %u%s; five sites patched",
	    settings.still, settings.every, settings.alternate ? "; alternating: on for n steps, off for n" : "");
}

void LogActorShadowRestSummary() {
	if (g_installed) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

}  // namespace obng::plugin
