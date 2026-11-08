#pragma once

// The CRT square root's dispatch in the game (FastSqrtLogic.h): one entry
// detour on _CIsqrt 0x00982C30.
//
//   [Route] Sqrt=1: the entry jumps to the generated dispatch; a finite,
//     non-negative argument under a fast control word gets its FSQRT and
//     nothing else, everything else continues in the original through the
//     trampoline (FastSqrt.cpp, InstallRouteFastSqrt).
//   [Shadow] Sqrt=1: the entry jumps to a thunk that saves the whole x87
//     state (FXSAVE), runs the original and the dispatch each from that
//     state, compares the 80-bit result, the control word and the exception
//     flags, records the call, restores the state and continues in the
//     original - the game always gets the original's result
//     (InstallShadowFastSqrt). Every call costs two square roots and two
//     FXRSTORs: a gate run, never play.
//
// Both patch the same entry; Load lets the shadow win when both are asked
// (PluginEntry.cpp).

#include "RouteSettings.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

UInt32 InstallRouteFastSqrt(const RouteSettings& settings, LogFn log);
UInt32 InstallShadowFastSqrt(const ShadowSettings& settings, LogFn log);

}  // namespace obng::plugin
