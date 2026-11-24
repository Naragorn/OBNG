#include "Lighting30Kind.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "engine/render/Lighting30KindLogic.h"
#include "engine/render/Lighting30ShadowLogic.h"

namespace obng::plugin {

namespace {

namespace l30 = obng::engine::render::lighting30_kind;
namespace shadow = obng::engine::render::lighting30_shadow;

using ChooserFn = int(__cdecl*)(UInt32 geometry, UInt32 a, UInt32 b);
using PrepareObjectFn = UInt8(__cdecl*)(UInt32 object, int kind, UInt32 a, UInt32 b, UInt32 c);
using GetPropertyFn = UInt32(__thiscall*)(UInt32 geometry, UInt32 type);
using AddHeadFn = void(__thiscall*)(UInt32 list, UInt32* slot);
using LightFn = UInt32(__thiscall*)(UInt32 property);
using AllocateFn = UInt32(__cdecl*)(UInt32 bytes);
using RecordFn = UInt32(__cdecl*)(UInt32 memory, UInt32 geometry, UInt32 pass, UInt32 flag, UInt32 lights, UInt32 light);

LogFn g_log = nullptr;
bool g_on = false;
bool g_shadows = false;
std::uint32_t g_bits = 0;
// Written where geometry is prepared (the main thread and the loader's); a lost count is a count, not a fault.
l30::Counts g_counts;
std::uint64_t g_marked = 0;
shadow::Counts g_shadowCounts;  // the render thread alone builds pass lists

template <typename T>
T At(UInt32 base, std::uint32_t offset) {
	T value;
	std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(base) + offset), sizeof(T));
	return value;
}

template <typename T>
void Put(UInt32 base, std::uint32_t offset, T value) {
	std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(base) + offset), &value, sizeof(T));
}

template <typename Fn>
Fn Game(std::uint32_t address) {
	return reinterpret_cast<Fn>(static_cast<std::uintptr_t>(address));
}

bool Has30() {
	return At<UInt8>(l30::kHas30, 0) != 0;
}

// The moved geometry's property is made here, by the game's own preparation with the kind given, so that it can be
// marked as the probe's. Whether the game's attach succeeded is handed back: the caller's own attach would find the
// property and answer yes for one that came out without a texture.
bool MakeMoved(UInt32 geometry, UInt32 a, UInt32 b) {
	const bool made = Game<PrepareObjectFn>(shadow::kPrepareObject)(geometry, l30::kLighting30, a, b, 0) != 0;
	const UInt32 property = Game<GetPropertyFn>(shadow::kGetProperty)(geometry, shadow::kShadeProperty);
	if (property != 0 && At<UInt32>(property, 0) == shadow::kPropertyVtable) {
		Put<UInt32>(property, shadow::kMarkOfProperty, shadow::kMark);
		++g_marked;
	}
	return made;
}

// In place of the object preparation's call of the chooser 0x007D1920.
int __cdecl ChooseKind(UInt32 geometry, UInt32 a, UInt32 b) {
	const int chosen = Game<ChooserFn>(l30::kChooser)(geometry, a, b);
	const int kind = l30::KindFor(chosen, Has30(), g_bits);
	l30::Count(g_counts, chosen, kind);
	if (kind == chosen) {
		return kind;
	}
	// 0 makes the caller leave the geometry as the preparation here left it.
	return MakeMoved(geometry, a, b) ? kind : 0;
}

// A record of its own for every actor shadow on the geometry, as ShadowLightShader's builder ends a list.
UInt32 AddShadowRecords(UInt32 property, UInt32 geometry, UInt16 pass) {
	UInt32 added = 0;
	for (UInt32 light = Game<LightFn>(shadow::kFirstLight)(property); light != 0; light = Game<LightFn>(shadow::kNextLight)(property)) {
		if (At<UInt8>(light, shadow::kLightIsActorShadow) == 0) {
			continue;
		}
		const UInt32 memory = Game<AllocateFn>(shadow::kAllocate)(shadow::kRecordBytes);
		if (memory == 0) {
			break;
		}
		UInt32 record = Game<RecordFn>(shadow::kRecord)(memory, geometry, 0, 0, 1, light);
		Put<UInt16>(record, shadow::kRecordPass, pass);
		Put<UInt8>(record, shadow::kRecordLast, 1);
		Game<AddHeadFn>(shadow::kAddHead)(property + shadow::kListOfProperty, &record);
		++added;
	}
	return added;
}

// In place of the list builder's call that adds the lighting record (0x00864078): ECX the list, the record's slot
// pushed. The game's own Lighting30 geometry costs one load and a compare more.
void __fastcall LightingRecordAdded(UInt32 list, void*, UInt32* slot) {
	Game<AddHeadFn>(shadow::kAddHead)(list, slot);
	const UInt32 property = list - shadow::kListOfProperty;
	const UInt32 mark = At<UInt32>(property, shadow::kMarkOfProperty);
	if (!shadow::IsMark(mark)) {
		return;
	}
	const UInt32 record = *slot;
	const bool drawn = shadow::ShadowPassesOn(At<UInt32>(shadow::kRenderPath, 0), At<UInt32>(shadow::kPassMask, 0));
	const UInt16 pass = shadow::Receives(g_shadows, mark, drawn, At<UInt16>(record, shadow::kRecordPass));
	const UInt32 added = pass != 0 ? AddShadowRecords(property, At<UInt32>(record, shadow::kRecordGeometry), pass) : 0;
	shadow::Count(g_shadowCounts, pass, added, At<UInt8>(record, shadow::kRecordLights));
}

// One call replaced by another: the bytes there must be the game's own call of `from`.
bool RedirectCall(std::uint32_t site, std::uint32_t from, std::uintptr_t to) {
	UInt8 expected[5] = {0xE8}, patch[5] = {0xE8};
	const UInt32 toOriginal = from - (site + 5);
	const UInt32 toOurs = static_cast<UInt32>(to) - (site + 5);
	std::memcpy(expected + 1, &toOriginal, 4);
	std::memcpy(patch + 1, &toOurs, 4);
	return Verify(site, expected, sizeof(expected)) && SafeWrite(site, patch, sizeof(patch));
}

}  // namespace

void InstallLighting30Kind(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_bits = l30::BitsOf(static_cast<int>(GetPrivateProfileIntA("Probe", "Lighting30Kinds", l30::Shipped(), iniPath)));
	if (!l30::On(g_bits)) {
		return;
	}
	if (!RedirectCall(l30::kCallSite, l30::kChooser, reinterpret_cast<std::uintptr_t>(&ChooseKind))) {
		log("Probe: Lighting30 kinds - the chooser's call at %08X is not the game's own or could not be written; nothing moved", l30::kCallSite);
		return;
	}
	g_log = log;
	g_on = true;
	const bool wanted = shadow::On(static_cast<int>(GetPrivateProfileIntA("Probe", "Lighting30Shadows", shadow::Shipped(), iniPath)));
	g_shadows = wanted && RedirectCall(shadow::kLightingRecordAdd, shadow::kAddHead, reinterpret_cast<std::uintptr_t>(&LightingRecordAdded));
	log("Probe: Lighting30 kinds - on at %08X: geometry the game gives %s%s%s is prepared with its one-pass Lighting30Shader instead "
	    "(when the card's flag at %08X is set) and marked. Actor shadows on it: %s. The picture changes: a measuring run only",
	    l30::kCallSite, (g_bits & l30::kMoveShadowLight) != 0 ? "ShadowLightShader " : "", (g_bits & l30::kMoveSkin) != 0 ? "SkinShader " : "",
	    (g_bits & l30::kMoveParallax) != 0 ? "ParallaxShader " : "", l30::kHas30,
	    g_shadows ? "a record a shadow light added to its lists (the builder's call at 00864078)"
	    : wanted  ? "none - the builder's call at 00864078 is not the game's own or could not be written"
	              : "none - off in OBNG.ini");
}

void LogLighting30KindSummary() {
	if (!g_on) {
		return;
	}
	g_log("Probe: Lighting30 kinds - since the start %llu geometries chosen for (the game's choice: ShadowLightShader %llu, SkinShader %llu, "
	      "ParallaxShader %llu, Lighting30Shader %llu, others %llu), %llu moved to Lighting30Shader, %llu marked; the card's flag is %s",
	      g_counts.asked, g_counts.shadowLight, g_counts.skin, g_counts.parallax, g_counts.lighting30, g_counts.other, g_counts.moved, g_marked,
	      Has30() ? "set" : "clear - the game makes no Lighting30Shader, nothing is moved");
	g_log("Probe: Lighting30 shadows (%s) - since the start %llu lists built for marked geometry, %llu of them with a lighting record that takes "
	      "actor shadows, %llu with at least one shadow light, %llu shadow records added; %llu lists with more lights than the shader draws "
	      "(%u), the most lights on one record %u",
	      g_shadows ? "on" : "off", g_shadowCounts.lists, g_shadowCounts.eligible, g_shadowCounts.withShadow, g_shadowCounts.records,
	      g_shadowCounts.leftOut, shadow::kLightsDrawn, g_shadowCounts.mostLights);
}

}  // namespace obng::plugin
