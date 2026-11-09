#pragma once

// The actors' skeleton update in the game (src/engine/scene/SceneUpdateLogic.h
// and SceneUpdate.h): one entry detour on NiAVObject::Update 0x00707370 that
// acts only for the call from ActorAnimData::ApplyAnimData 0x00471F20 (the
// return site 0x00471FBE, one actor's skeleton a call) and hands every other
// caller to the original.
//
//   [Route] ActorUpdate=1: the subtree's order (the flat array) is built once
//     per skeleton root and checked against the tree's shape on every call;
//     when it matches, the flat run does the controllers, the world
//     transforms, the geometry bounds and the nodes' bound merges and the
//     parent's upward pass follows - the same calls in the same order, the
//     transforms bit for bit. A root that is not a plain node or a shape the
//     order cannot describe goes to the original (FastSceneUpdate.cpp,
//     InstallRouteActorUpdate).
//   [Shadow] ActorUpdate=1: the subtree's world transforms and bounds are
//     saved, the original runs, its results are saved, the state is put
//     back, the flat run runs on it (without the controllers, which ran
//     already), the two results are compared per object bit for bit, and
//     the original's results are put back for the game. The Other subtrees
//     (the heads' BSFaceGenNiNode) and the collision objects run twice:
//     a gate run, never play (InstallShadowActorUpdate).
//
// Both patch the same entry; Load lets the shadow win when both are asked,
// and neither is installed beside [Probe] SceneUpdate, whose detour sits on
// the same entry (PluginEntry.cpp).

#include "RouteSettings.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

UInt32 InstallRouteActorUpdate(const RouteSettings& settings, LogFn log);
UInt32 InstallShadowActorUpdate(const ShadowSettings& settings, LogFn log);

// The route's summary line (calls, routed, orders built and rebuilt, the
// time in the routed calls), when the route is on; nothing otherwise.
void LogActorUpdateSummary();

}  // namespace obng::plugin
