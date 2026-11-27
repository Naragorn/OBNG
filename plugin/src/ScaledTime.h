#pragma once

// NiTimeController's scaled time 0x007155A0 without its critical section
// (engine/animation/ScaledTimeLogic.h has the reading and the computation).
//
//   [Route] ScaledTime=1: the rebuild runs instead - on the game's main thread
//     with a memo of its own (the original's one-entry memo, seeded from its
//     statics), on every other thread without one. A call under an x87
//     precision control other than 24 bits goes to the original.
//     ScaledTimeAlternate=1: the rebuild in every second summary window only.
//     [Probe] ScaledTimeBench=1 beside it: at the first update step both are
//     timed on a controller of the probe's own, a million calls each.
//   [Shadow] ScaledTime=1: the original runs; on the main thread the rebuild
//     runs beside it from the same fields and the same memo, and its result,
//     the three fields it writes and the memo are compared bit for bit. Wins
//     when both are asked.
//
// Class A on the main thread (the gate: the shadow's mismatches). On another
// thread the memo is not replayed: a call that would have hit the memo with
// another reverse flag gets its own flag's result (the logic header's note).

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallScaledTime(const char* iniPath, LogFn log);
void LogScaledTimeSummary();

}  // namespace obng::plugin
