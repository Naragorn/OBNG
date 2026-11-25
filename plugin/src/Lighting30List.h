#pragma once

// [Probe] Lighting30MostLights (src/engine/render/Lighting30ListLogic.h): a geometry [Probe] Lighting30Kinds moved
// to the one-pass shader has its pass list built by the game's multi-pass chain, and is drawn by ShadowLightShader,
// for as long as the one pass could not draw it - under more lights than its one-pass program holds (seven or
// eight, read from the program's own constant table), or with a glow the bloom is to take. The one-pass property's
// pass list getter (its vtable slot at 0x00A957C8) goes through this probe; it decides when a marked property's
// list is stale, empties the property's lists and swaps the geometry's shader when the builder changes, calls the
// getter of the builder chosen, and looks at the record a one-pass build made: which program it gets says how many
// lights it is drawn with (each program's count read from its constant table as the loader makes it). The game's own one-pass geometry costs a load and a compare more. On with [Probe]
// Lighting30Kinds only. Counts what it decided.

#include <cstdint>

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// kindsOn: whether [Probe] Lighting30Kinds moves anything; without it nothing is installed.
void InstallLighting30List(const char* iniPath, LogFn log, bool kindsOn);
void LogLighting30ListSummary();
// From the one-pass builder's call that adds its lighting record, for a marked property: the record's pass id and
// light count.
void NoteOnePassRecord(UInt32 property, UInt16 pass, UInt8 lights);
// The lights a lighting record of this pass id is drawn with for this property (its program's count, the setup's cap).
std::uint32_t Lighting30LightsDrawn(UInt32 property, UInt16 pass);
// From the pixel program maker's calls while the shader package is loaded: a program's LightData registers.
void NoteProgramLights(const char* name, std::uint32_t lightRegisters);

}  // namespace obng::plugin
