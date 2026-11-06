#pragma once

// The grass fade fix ([Fix] GrassFade, GrassFadePatchLogic.h): the game's
// grass vertex shaders replaced, as the game binds them, by twins that fade
// a blade by its distance from the camera.

#include "FixSettings.h"
#include "ShadowTimeInfo.h"

namespace obng::plugin {

// Starts the wait for the game's device and hooks it when it is there
// (CreateVertexShader and SetVertexShader in the device's method table).
// False if the wait could not be started.
bool InstallGrassFadeFix(const FixSettings& settings, LogFn log);

// Render thread: 0 when the vertex shader bound last is not a twin, else
// the factor the twin puts on the squared distance (2 for the game's reach,
// 1 for the INI's). For the grass probe, which computes what the bound
// shader makes of a blade.
int GrassFadeFixBoundTwin();

}  // namespace obng::plugin
