#pragma once
// [Probe] SceneOrderCheck (src/engine/scene/SceneOrderCheckLogic.h): the flat
// skeleton update's check of its order against the tree, timed, and with 2
// run again at once (the warm second). FastSceneUpdate.cpp calls Checked
// instead of Matches.

#include "engine/scene/SceneUpdate.h"
#include "ShadowTimeInfo.h"
#include "engine/scene/SceneUpdateLogic.h"

namespace obng::plugin {

void InstallSceneOrderCheck(const char* iniPath, LogFn log);

// Matches(order, tree), timed when the probe is on.
bool SceneOrderChecked(const engine::scene::scene_update::Order& order, const engine::scene::scene_update::engine::Tree& tree);

void LogSceneOrderCheckSummary();

}  // namespace obng::plugin
