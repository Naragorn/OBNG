#pragma once

// [Route] ActorFarGlide: a far actor shown on its way between its updates
// (src/engine/scene/ActorFarGlideLogic.h). Called by ActorFarUpdate.cpp on
// the thread of the actors' loop; on with [Route] ActorFarWhole unless
// ActorFarGlide=0.

#include "ShadowTimeInfo.h"
#include "Types.h"
#include "engine/scene/ActorFarUpdateLogic.h"

namespace obng::plugin {

void ActorFarGlideInit(bool on, LogFn log);

// A far actor's call was left out: its subtree goes to where the animation's
// summed step so far puts it.
void ActorFarGlideSkipped(engine::scene::actor_far_update::Entry& entry, UInt32 actor);

// An update of a far actor is about to run (before the travel note, which
// moves the entry's last position). `step`: the summed step it takes, or
// null.
void ActorFarGlideAtUpdate(engine::scene::actor_far_update::Entry& entry, UInt32 actor, const float* step);

void LogActorFarGlideSummary();

}  // namespace obng::plugin
