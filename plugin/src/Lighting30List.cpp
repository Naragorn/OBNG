#include "Lighting30List.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Memory.h"
#include "engine/render/Lighting30ListLogic.h"
#include "engine/render/Lighting30ShadowLogic.h"

namespace obng::plugin {

namespace {

namespace list = obng::engine::render::lighting30_list;
namespace shadow = obng::engine::render::lighting30_shadow;

using GetterFn = UInt32(__thiscall*)(UInt32 property, UInt32 geometry, UInt32 mask, UInt32 count, UInt32 build);
using ClearFn = void(__thiscall*)(UInt32 property);
using DefinitionFn = UInt32(__cdecl*)(int kind);
using LightFn = UInt32(__thiscall*)(UInt32 property);
using DeleteFn = void(__thiscall*)(UInt32 object, UInt32 free);

// The lighting record the one-pass builder made during the getter's call in progress; property 0: none.
struct Built {
	UInt32 property = 0;
	UInt16 pass = 0;
	UInt8 lights = 0;
};

LogFn g_log = nullptr;
bool g_on = false;
std::uint32_t g_most = static_cast<std::uint32_t>(list::Shipped());
UInt32 g_onePassShader = 0, g_chainShader = 0;  // the renderer makes them once; asked for until both are there
Built g_built;
// The LightData registers of SM3000 .. SM3019 as the loader made them; not seen: the least any draws is taken.
// Written while the renderer makes its shaders, read when lists are built - after it.
bool g_seen[list::kPrograms] = {};
std::uint16_t g_lightRegisters[list::kPrograms] = {};
list::Counts g_counts;  // the render thread alone asks for pass lists

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

LONG* ReferencesOf(UInt32 object) {
	return reinterpret_cast<LONG*>(static_cast<std::uintptr_t>(object) + list::kReferencesOfObject);
}

UInt32 ShaderOfKind(int kind) {
	const UInt32 definition = Game<DefinitionFn>(list::kShaderDefinition)(kind);
	return definition != 0 ? At<UInt32>(definition, list::kShaderOfDefinition) : 0;
}

// The lights the one-pass builder would put behind the scene's first: the same walk, the same test.
std::uint32_t OtherLights(UInt32 property) {
	std::uint32_t count = 0;
	for (UInt32 light = Game<LightFn>(shadow::kFirstLight)(property); light != 0; light = Game<LightFn>(shadow::kNextLight)(property)) {
		if (At<UInt8>(light, shadow::kLightIsActorShadow) == 0) {
			++count;
		}
	}
	return count;
}

// Whether the chain would give this geometry the pass that writes its glow into the frame's alpha. Without bloom
// nothing of the geometry is read.
bool GlowsInBloom(UInt32 geometry, UInt32 mask) {
	if (At<UInt8>(list::kBloom, 0) == 0) {
		return false;
	}
	const UInt32 state = At<UInt32>(geometry, list::kStateOfGeometry);
	const UInt32 material = state != 0 ? At<UInt32>(state, list::kMaterialOfState) : 0;
	const bool black = material != 0 && list::SameColour(At<float>(material, list::kEmissiveOfMaterial), At<float>(material, list::kEmissiveOfMaterial + 4),
	                                                    At<float>(material, list::kEmissiveOfMaterial + 8), At<float>(list::kBlack, 0),
	                                                    At<float>(list::kBlack, 4), At<float>(list::kBlack, 8));
	return list::GlowsInBloom(true, mask, material != 0, black);
}

// The LightData registers of a lighting pixel program as the loader made it; 0 when it was not seen.
std::uint32_t LightRegisters(std::uint32_t program) {
	return program < list::kPrograms && g_seen[program] ? g_lightRegisters[program] : 0;
}

// The lights the record just built will be drawn with: the picker's program for its pass id and the property's
// specular, as the shader's setup hands it over.
std::uint32_t DrawnWith(UInt32 property, UInt16 pass) {
	const bool specular = (At<UInt32>(shadow::kPassMask, 0) & list::kSpecularPass) != 0 && (At<UInt32>(property, shadow::kFlagsOfProperty) & list::kSpecularFlag) != 0;
	return list::DrawnWith(At<UInt32>(list::kSetupCap, 0), LightRegisters(list::ProgramOf(pass, specular)));
}

// The geometry's shader pointer set as the game's own smart pointer sets it: the new one held first, the old one
// let go and deleted when that was its last reference (the two shaders here are held by their definitions).
void GiveShader(UInt32 geometry, UInt32 shader) {
	const UInt32 old = At<UInt32>(geometry, list::kShaderOfGeometry);
	InterlockedIncrement(ReferencesOf(shader));
	Put<UInt32>(geometry, list::kShaderOfGeometry, shader);
	if (old != 0 && InterlockedDecrement(ReferencesOf(old)) == 0) {
		Game<DeleteFn>(At<UInt32>(At<UInt32>(old, 0), 0))(old, 1);
	}
}

void HandOver(UInt32 property, UInt32 geometry, bool toChain) {
	Game<ClearFn>(list::kClearLists)(property);
	GiveShader(geometry, toChain ? g_chainShader : g_onePassShader);
}

// The one-pass getter for a marked property, and what its build tells: the lights its record's program draws go
// into the mark, and a record with more lights than that has its list built by the chain after all.
UInt32 OnePassList(UInt32 property, UInt32 geometry, UInt32 mask, UInt32 count, UInt32 build) {
	g_built.property = 0;
	const UInt32 answer = Game<GetterFn>(list::kOnePassGetter)(property, geometry, mask, count, build);
	if (g_built.property != property) {
		return answer;
	}
	const std::uint32_t drawn = DrawnWith(property, g_built.pass);
	Put<UInt32>(property, shadow::kMarkOfProperty, shadow::MarkWith(drawn));
	if (!list::Overflows(g_built.lights, g_most, drawn)) {
		return answer;
	}
	list::CountOverflow(g_counts);
	HandOver(property, geometry, true);
	return Game<GetterFn>(list::kChainGetter)(property, geometry, mask, count, build);
}

// In the slot of Lighting30ShaderProperty's pass list getter: ECX the property; the geometry, the pass mask, where
// the count goes and whether records are built - handed on as they came.
UInt32 __fastcall PassList(UInt32 property, void*, UInt32 geometry, UInt32 mask, UInt32 count, UInt32 build) {
	const UInt32 mark = At<UInt32>(property, shadow::kMarkOfProperty);
	if (!shadow::IsMark(mark)) {
		return Game<GetterFn>(list::kOnePassGetter)(property, geometry, mask, count, build);
	}
	if (g_onePassShader == 0 || g_chainShader == 0) {
		g_onePassShader = ShaderOfKind(list::kOnePassKind);
		g_chainShader = ShaderOfKind(list::kChainKind);
	}
	const list::Carries carries = list::CarriesFor(At<UInt32>(geometry, list::kShaderOfGeometry), g_onePassShader, g_chainShader);
	const UInt32 key = At<UInt32>(property, list::kKeyOfProperty);
	const bool decides = list::Decides(true, carries, key);
	const list::Seen seen = {decides ? list::RecordLights(OtherLights(property)) : 0,
	                         list::LimitFor(g_most, shadow::LightsKnown(mark), At<UInt32>(list::kSetupCap, 0)), decides && GlowsInBloom(geometry, mask)};
	const list::Step step = list::StepFor(true, carries, key, seen.recordLights, seen.limit, seen.glowsInBloom);
	list::Count(g_counts, true, carries, key, seen, step);
	const bool chain = step.builder == list::Builder::kChain;
	if (step.change) {
		HandOver(property, geometry, chain);
	}
	if (chain) {
		return Game<GetterFn>(list::kChainGetter)(property, geometry, mask, count, build);
	}
	return carries == list::Carries::kOther ? Game<GetterFn>(list::kOnePassGetter)(property, geometry, mask, count, build)
	                                        : OnePassList(property, geometry, mask, count, build);
}

}  // namespace

void NoteOnePassRecord(UInt32 property, UInt16 pass, UInt8 lights) {
	g_built = {property, pass, lights};
}

std::uint32_t Lighting30LightsDrawn(UInt32 property, UInt16 pass) {
	return DrawnWith(property, pass);
}

void NoteProgramLights(const char* name, std::uint32_t lightRegisters) {
	const std::uint32_t program = list::ProgramNumberOf(name);
	if (program < list::kPrograms) {
		g_lightRegisters[program] = static_cast<std::uint16_t>(lightRegisters < 0xFFFF ? lightRegisters : 0);
		g_seen[program] = true;
	}
}

void InstallLighting30List(const char* iniPath, LogFn log, bool kindsOn) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr || !kindsOn) {
		return;
	}
	s_done = true;
	g_most = list::MostOf(static_cast<int>(GetPrivateProfileIntA("Probe", "Lighting30MostLights", list::Shipped(), iniPath)));
	const UInt32 theirs = list::kOnePassGetter;
	const UInt32 ours = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&PassList));
	UInt8 expected[4];
	std::memcpy(expected, &theirs, sizeof(expected));
	if (!Verify(list::kGetterSlot, expected, sizeof(expected)) || !SafeWrite(list::kGetterSlot, &ours, sizeof(ours))) {
		log("Probe: Lighting30 lists - the one-pass property's getter slot at %08X does not hold the game's %08X or could not be written; "
		    "a moved geometry under more lights than its program draws loses the last of them",
		    list::kGetterSlot, list::kOnePassGetter);
		return;
	}
	g_log = log;
	g_on = true;
	log("Probe: Lighting30 lists - on at %08X (the key %u): a moved geometry under more lights than its one-pass program draws, or with a "
	    "glow the bloom takes, has its pass list built by the game's chain (%08X) and is drawn by ShadowLightShader for as long as that holds",
	    list::kGetterSlot, g_most, list::kChainGetter);
}

void LogLighting30ListSummary() {
	if (!g_on) {
		return;
	}
	char programs[128];
	int used = 0;
	for (std::uint32_t i = 0; i < list::kPrograms && used < static_cast<int>(sizeof(programs)) - 8; ++i) {
		if (g_seen[i]) {
			used += std::snprintf(programs + used, sizeof(programs) - used, "%s%u:%u", used != 0 ? " " : "", i, g_lightRegisters[i] / 2u);
		}
	}
	programs[used] = 0;
	g_log("Probe: Lighting30 lists (the key %u; more lights than the program draws, or a glow under bloom: the chain) - since the start %llu "
	      "stale lists of moved geometry decided on, %llu of them built by the game's chain (%llu for their lights - %llu of those found "
	      "by the record built -, %llu for a glow); %llu geometries handed to ShadowLightShader, %llu back; the most lights on one list "
	      "%u; the lights of the programs as made (number:lights) %s; bloom %s; %llu calls for a moved geometry under another shader",
	      g_most, g_counts.decided, g_counts.chain, g_counts.manyLights, g_counts.overflowed, g_counts.glow, g_counts.toChain,
	      g_counts.toOnePass, g_counts.mostLights, used != 0 ? programs : "none", At<UInt8>(list::kBloom, 0) != 0 ? "on" : "off", g_counts.other);
}

}  // namespace obng::plugin
