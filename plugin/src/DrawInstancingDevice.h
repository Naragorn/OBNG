#pragma once
// The Direct3D 9 side of [Route] DrawInstancing (DrawInstancing.h): what the
// device holds for a draw (mode 1's comparison), whether its programs are
// SLS2003's, the instance buffer and the instanced draw itself with
// everything it changes put back. Render thread only. The device is a
// std::uint32_t handle; the COM calls go through d3d9.h's interfaces.
#include <cstdint>

#include "DrawInstancingLogic.h"
#include "ShadowTimeInfo.h"

namespace obng::plugin::draw_instancing {

// One indexed device draw as the whole draw makes it (d3d9.h
// DrawIndexedPrimitive's arguments).
struct IndexedCall {
	std::uint32_t type = 0;
	std::int32_t base = 0;
	std::uint32_t minIndex = 0;
	std::uint32_t vertices = 0;
	std::uint32_t start = 0;
	std::uint32_t count = 0;
};

// What mode 1 compares between a group's records: the vertex registers
// c0..c33, the pixel registers c1, c2, c7, the textures of stages 0, 1, 6, 7
// and the programs and declaration (as identities: the references are
// released at once).
struct DeviceState {
	float vs[kVertexRegisters][4];
	float ps[3][4];
	std::uint32_t textures[4];
	std::uint32_t vertexShader;
	std::uint32_t pixelShader;
	std::uint32_t declaration;
};

bool ReadDeviceState(std::uint32_t device, DeviceState* out);

// The device's vertex registers c0..c33.
bool ReadVertexRegisters(std::uint32_t device, float (&out)[kVertexRegisters][4]);

// Whether the device's current vertex and pixel program are SLS2003.vso
// (996 bytes, FNV-1a 55A67685) and the SLS2003.pso the instanced pixel
// program was translated from (864 bytes, C39FE354); cached per program object.
bool HoldsSls2003(std::uint32_t device);

// The instance rows written into the instance buffer in one lock; the index
// of the first in *first. False when there is no room or the buffer cannot
// be made or locked.
bool WriteInstances(std::uint32_t device, const InstanceData* rows, std::uint32_t count, std::uint32_t* first);

// The draw `call` for `count` instances from `first` on: OBNG's programs, the
// device's declaration with the instance stream added, the derived constants
// and the shadow rows' form (c34.x: 1 each copy's own world rows, 0 the
// derived rows); afterwards the programs, the declaration, the streams and
// c0..c34 (given in `registers`) as they were. False (with the reason) when
// nothing was drawn.
bool DrawInstanced(std::uint32_t device, const Derived& derived, const float (&registers)[kVertexRegisters][4], float shadowMode, std::uint32_t first,
                   std::uint32_t count, const IndexedCall& call, Fallback* why);

// Hooks the device's Reset once (its vtable slot 16), so that the instance
// buffer - the one default-pool object - is released before the engine's
// Reset runs and made anew afterwards.
void EnsureDeviceResetHook(std::uint32_t device, LogFn log);

}  // namespace obng::plugin::draw_instancing
