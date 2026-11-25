#include "Lighting30Specular.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "Memory.h"
#include "engine/render/Lighting30ClipLogic.h"
#include "engine/render/Lighting30ShadowLogic.h"
#include "engine/render/Lighting30SpecularLogic.h"

namespace obng::plugin {

namespace {

namespace spec = obng::engine::render::lighting30_specular;
namespace clip = obng::engine::render::lighting30_clip;
namespace shadow = obng::engine::render::lighting30_shadow;

using LookupFn = UInt32(__thiscall*)(UInt32 package, const char* name);
using SetupFn = UInt32(__thiscall*)(UInt32 shader, UInt32 geometry, UInt32 skin, UInt32 buffer, UInt32 state, UInt32 effects, UInt32 world,
                                    UInt32 bound);

inline constexpr std::uint32_t kStateShadeProperty = 0x18;  // NiPropertyState: the shader property
inline constexpr std::uint32_t kGeometryBound = 0x20;       // NiAVObject: the world bound, centre and radius

LogFn g_log = nullptr;
bool g_on = false;       // the programs are rewritten as made
bool g_setup = false;    // the setup writes the fade
bool g_making = false;   // the loader is making one of Lighting30Shader's pixel programs (one thread, the renderer's start)
UInt8* g_entries[spec::kSlots] = {};  // the rewritten entries, kept: made once a program, a few KB
std::uint32_t g_entryBytes[spec::kSlots] = {};
spec::Counts g_counts;  // programs: the loader's thread; setups: the render thread alone

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

// An entry laid out as the package's: the name's 0x100 bytes, the length, the program with the fade's multiply.
UInt32 Rewritten(std::uint32_t slot, UInt32 entry) {
	const std::size_t count = clip::TokensOf(At<UInt32>(entry, clip::kEntryLength));
	const std::uint32_t* tokens = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(entry) + clip::kEntryCode);
	const spec::Found found = spec::FindGlossMultiply(tokens, count);
	spec::CountProgram(g_counts, found.outcome);
	if (found.outcome != spec::Outcome::kFade) {
		return entry;
	}
	const std::uint32_t bytes = clip::kEntryCode + static_cast<std::uint32_t>(count + spec::kAdded) * 4;
	if (g_entries[slot] == nullptr || g_entryBytes[slot] < bytes) {
		g_entries[slot] = static_cast<UInt8*>(std::malloc(bytes));  // a device reset makes the programs again: kept until then
		g_entryBytes[slot] = g_entries[slot] != nullptr ? bytes : 0;
	}
	if (g_entries[slot] == nullptr) {
		return entry;
	}
	std::memcpy(g_entries[slot], reinterpret_cast<const void*>(static_cast<std::uintptr_t>(entry)), clip::kEntryLength);
	auto* out = reinterpret_cast<std::uint32_t*>(g_entries[slot] + clip::kEntryCode);
	const std::size_t n = spec::WithFade(tokens, count, out, count + spec::kAdded);
	if (n == 0) {
		return entry;
	}
	const UInt32 length = static_cast<UInt32>(n * 4);
	std::memcpy(g_entries[slot] + clip::kEntryLength, &length, sizeof(length));
	return static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(g_entries[slot]));
}

// In place of the pixel program maker's call of the package's lookup: ECX the package, the name pushed.
UInt32 __fastcall LookupForMaker(UInt32 package, void*, const char* name) {
	const UInt32 entry = Game<LookupFn>(clip::kLookup)(package, name);
	const std::uint32_t slot = g_making ? spec::SlotOf(name) : spec::kNoSlot;
	return entry != 0 && slot < spec::kSlots ? Rewritten(slot, entry) : entry;
}

// In the slot of Lighting30Shader's setup: the game's setup, then a moved geometry's specular toggle faded.
UInt32 __fastcall Setup(UInt32 shader, void*, UInt32 geometry, UInt32 skin, UInt32 buffer, UInt32 state, UInt32 effects, UInt32 world, UInt32 bound) {
	const UInt32 result = Game<SetupFn>(spec::kSetup)(shader, geometry, skin, buffer, state, effects, world, bound);
	const UInt32 property = state != 0 ? At<UInt32>(state, kStateShadeProperty) : 0;
	if (property == 0 || geometry == 0 || !shadow::IsMark(At<UInt32>(property, shadow::kMarkOfProperty))) {
		return result;
	}
	const float written = At<float>(spec::kSpecularToggle, 0);
	if (written != 1.0f || !spec::FadesPass(At<UInt32>(spec::kCurrentPass, 0))) {
		return result;
	}
	const float camera[3] = {At<float>(spec::kCamera, 0), At<float>(spec::kCamera, 4), At<float>(spec::kCamera, 8)};
	const float center[3] = {At<float>(geometry, kGeometryBound), At<float>(geometry, kGeometryBound + 4), At<float>(geometry, kGeometryBound + 8)};
	const float distance = spec::DistanceToBound(camera, center, At<float>(geometry, kGeometryBound + spec::kBoundRadius));
	const float fade = spec::Fade(distance, At<float>(spec::kFadeNear, 0), At<float>(spec::kFadeFar, 0));
	Put<float>(spec::kSpecularToggle, 0, spec::ToggleFor(written, fade));
	spec::CountSetup(g_counts, fade);
	return result;
}

bool RedirectLookup() {
	UInt8 expected[5] = {0xE8}, patch[5] = {0xE8};
	const UInt32 toOriginal = clip::kLookup - (spec::kLookupCall + 5);
	const UInt32 toOurs = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&LookupForMaker)) - (spec::kLookupCall + 5);
	std::memcpy(expected + 1, &toOriginal, 4);
	std::memcpy(patch + 1, &toOurs, 4);
	return Verify(spec::kLookupCall, expected, sizeof(expected)) && SafeWrite(spec::kLookupCall, patch, sizeof(patch));
}

bool TakeSetupSlot() {
	const UInt32 theirs = spec::kSetup;
	const UInt32 ours = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&Setup));
	UInt8 expected[4];
	std::memcpy(expected, &theirs, sizeof(expected));
	return Verify(spec::kSetupSlot, expected, sizeof(expected)) && SafeWrite(spec::kSetupSlot, &ours, sizeof(ours));
}

}  // namespace

void SetMakingLighting30Program(bool making) {
	g_making = making && g_on;
}

void InstallLighting30Specular(const char* iniPath, LogFn log, bool kindsOn) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (!spec::On(static_cast<int>(GetPrivateProfileIntA("Probe", "Lighting30SpecularFade", spec::Shipped(), iniPath)), kindsOn)) {
		return;
	}
	g_log = log;
	g_on = RedirectLookup();
	g_setup = g_on && TakeSetupSlot();
	if (!g_on || !g_setup) {
		log("Probe: Lighting30 specular fade - the maker's lookup call at %08X (%s) or the shader's setup slot at %08X (%s) is not the game's "
		    "own or could not be written; a moved geometry's specular does not fade with distance",
		    spec::kLookupCall, g_on ? "taken" : "not taken", spec::kSetupSlot, g_setup ? "taken" : "not taken");
		return;
	}
	log("Probe: Lighting30 specular fade - on at %08X and %08X: Lighting30Shader's specular programs multiply their specular by "
	    "ToggleADTS.w as they are made, and a moved geometry's setup writes the game's distance fade there (fSpecularLOD1 .. 2)",
	    spec::kLookupCall, spec::kSetupSlot);
}

void LogLighting30SpecularSummary() {
	if (!g_on) {
		return;
	}
	g_log("Probe: Lighting30 specular fade - %llu lighting programs made through the probe: %llu with the fade's multiply, %llu without "
	      "specular, %llu without the table's names, %llu of another shape; %llu setups of a moved geometry's specular program, %llu of "
	      "them faded, %llu to 0; the fade from %.0f to %.0f units",
	      g_counts.programs, g_counts.faded, g_counts.noSpecular, g_counts.noTable, g_counts.broken, g_counts.setups, g_counts.dimmed,
	      g_counts.dark, static_cast<double>(At<float>(spec::kFadeNear, 0)), static_cast<double>(At<float>(spec::kFadeFar, 0)));
}

}  // namespace obng::plugin
