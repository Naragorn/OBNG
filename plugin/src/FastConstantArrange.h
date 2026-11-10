#pragma once

// The constant map's entry arranging 0x009A97B0 left out when it has nothing
// to swap (src/engine/render/ConstantMapArrangeLogic.h): one entry detour.
//
//   [Route] ConstantArrange=1: the test runs; when the entries are arranged
//     the function returns without the walk, otherwise the original runs.
//   [Shadow] ConstantArrange=1: the test runs, then the original always
//     runs, and the entry array and its two counts are compared with what
//     they were: "arranged" with a change afterwards is a mismatch. The
//     shadow wins when both are asked.

#include "RouteSettings.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallConstantArrange(const ShadowSettings& shadow, const RouteSettings& route, LogFn log);
void LogConstantArrangeSummary();

}  // namespace obng::plugin
