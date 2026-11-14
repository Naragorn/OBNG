#pragma once

// [Route] ActorFarGlide: a far actor shown on its way between its updates
// (src/engine/scene/ActorFarGlideLogic.h). Called by ActorFarUpdate.cpp on
// the thread of the actors' loop; on with [Route] ActorFarWhole unless
// ActorFarGlide=0.

#include "ShadowTimeInfo.h"
#include "Types.h"
#include "engine/scene/ActorFarUpdateLogic.h"

namespace obng::plugin {

void ActorFarGlideInit(bool on, unsigned samples, LogFn log);

// Whether any actor has an offset in its node now (about; for the paths of
// an actor's update that do not look its entry up).
bool ActorFarGlideAny();

// The actor leaves the glide (it came near, its process changed): the offset
// is taken out of its node.
void ActorFarGlideRelease(engine::scene::actor_far_update::Entry& entry, UInt32 actor);

// A far actor's call was left out: its node goes to where the animation's
// summed step so far (or, without one, its steady speed over `elapsed` seconds
// since its last update) puts it.
void ActorFarGlideSkipped(engine::scene::actor_far_update::Entry& entry, UInt32 actor, float elapsed);

// An update of a far actor is about to run (before the travel note, which
// moves the entry's last position): the offset is taken out of its node, the
// gain measured. `step`: the summed step it takes, or null; `seconds`: the
// time it stands for.
void ActorFarGlideAtUpdate(engine::scene::actor_far_update::Entry& entry, UInt32 actor, const float* step, float seconds);

void LogActorFarGlideSummary();

}  // namespace obng::plugin
