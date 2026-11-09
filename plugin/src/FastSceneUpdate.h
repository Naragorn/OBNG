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
//   [Shadow] ActorUpdate=1: the flat run runs once, with the original's
//     arithmetic beside it - every world transform also computed by the
//     original NiTransform compose 0x0053D7A0 and compared bit for bit (the
//     original's written), every node's bound recomputed afterwards by the
//     original NiNode::UpdateWorldBound 0x0070A360 and compared (the
//     original's kept). The game gets the original's numbers; the calls and
//     their order are the rebuild's, held against the original's bytes by
//     the oracle test. A subtree cannot run twice in a frame (controllers
//     fire once, the heads step the face animation by the tick), so this is
//     the gate (InstallShadowActorUpdate).
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
