#pragma once

// The [Fix] switches: changes to what the game draws that correct a fault of
// the engine itself, on in play. Read in Plugin.cpp, decided in
// PluginEntry.cpp (Load). No Windows or engine dependency: PluginEntryTest
// includes this.

namespace obng::plugin {

struct FixSettings {
	// [Fix] GrassFade: the grass vertex shaders fade a blade by its distance
	// from the camera instead of by the length of its clip-space position,
	// which depends on the frustum and on the blade's place in the picture
	// (GrassFadePatchLogic.h). 0 leaves the game's shaders.
	bool grassFade = true;
	// [Fix] GrassFadeReach=ini: fGrassStartFadeDistance / fGrassEndDistance
	// are distances in units (grass reaches 41 % further than the game has
	// it in the middle of the view). The default, game, keeps the game's
	// reach straight ahead.
	bool grassFadeIniReach = false;

	constexpr bool Asked() const { return grassFade; }
};

}  // namespace obng::plugin
