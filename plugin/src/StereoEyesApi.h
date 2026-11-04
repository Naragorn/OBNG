#pragma once

// The C interface of OBNG.dll for single-pass stereo (docs/subsystems/
// single-pass-stereo.md, step 4): the VR side (OBVR) owns the eye data - the
// step from the pass camera to the second eye and one viewport per eye on
// its double-wide target - and hands it to OBNG once per world render; OBNG's
// stereo draw route draws every partition twice, switching the renderer's
// view translation, the device's VIEW transform and the viewport between the
// two draws (StereoEyeSwitchLogic.h). Plain C so that OBVR can bind the three
// functions with GetProcAddress and no OBNG header beyond this one.
//
// Contract:
//   - Eye 0 is the camera of the pass as the engine set it (Do_SetCameraData
//     0x00762640): offset zero. Eye 1 is that camera moved by secondEyeOffset,
//     a world-space vector in game units (OBVR's g_dualShift as it stands).
//   - The viewports are in pixels of the render target the pass draws into,
//     in D3DVIEWPORT9 order; eye 0's must be the one the engine's own
//     SetViewport would have left (the camera's port rect on that target), as
//     the first draw of every partition runs before any switch.
//   - Set the eyes right before the world render and clear them right after.
//     While they are set, a draw doubles only when the device's current
//     viewport rectangle is viewport[0] (one GetViewport per draw call): the
//     passes inside RenderScene that draw into their own targets - the
//     actor shadow maps, the water pre-pass - draw once. Passes that share
//     eye 0's rectangle (the 2D layer over the world target, say) still
//     double, so the clear after the world render stays the caller's.
//   - All three functions are called on the render thread only (the thread
//     that runs the draws); nothing here locks.

#include <stdint.h>

// OBNG.dll's own build exports the three functions; everyone else (OBVR, the
// tests) sees plain declarations and binds them with GetProcAddress.
#if defined(OBNG_BUILDING_DLL)
#define OBNG_STEREO_API __declspec(dllexport)
#else
#define OBNG_STEREO_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum { kObngStereoEyesVersion = 1 };

// D3DVIEWPORT9's layout (d3d9types.h): X, Y, Width, Height, MinZ, MaxZ.
typedef struct ObngStereoViewport {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	float minZ;
	float maxZ;
} ObngStereoViewport;

typedef struct ObngStereoEyes {
	uint32_t size;              // sizeof(ObngStereoEyes)
	uint32_t version;           // kObngStereoEyesVersion
	float secondEyeOffset[3];   // world space, game units: from eye 0 (the pass camera) to eye 1
	ObngStereoViewport viewport[2];  // eye 0, eye 1
} ObngStereoEyes;

enum ObngStereoResult {
	kObngStereoOk = 0,
	kObngStereoNull = 1,       // a null pointer
	kObngStereoSize = 2,       // size != sizeof(ObngStereoEyes)
	kObngStereoVersion = 3,    // version != kObngStereoEyesVersion
	kObngStereoViewport = 4,   // a viewport with zero width or height, or MinZ/MaxZ outside 0..1 or inverted
	kObngStereoOffset = 5,     // an offset component that is not finite
	kObngStereoRouteOff = 6,   // [Route] StereoDraw is off or StereoEyes is 1: the draws cannot double
};

// What the route can do: 0 = the stereo draw route is off, 1 = on with one
// eye (the 1:1 gate), 2 = on and allowed to draw twice. OBVR renders one
// pass only when this is 2 and SetEyes returned kObngStereoOk.
OBNG_STEREO_API uint32_t ObngStereoPlan(void);

// The eye data for every draw from now until ObngStereoClearEyes. Copied;
// the caller keeps its memory. Returns an ObngStereoResult; on anything but
// kObngStereoOk the previous eye data stays as it was.
OBNG_STEREO_API uint32_t ObngStereoSetEyes(const ObngStereoEyes* eyes);

// Back to one draw per partition.
OBNG_STEREO_API void ObngStereoClearEyes(void);

typedef uint32_t (*ObngStereoPlanFn)(void);
typedef uint32_t (*ObngStereoSetEyesFn)(const ObngStereoEyes* eyes);
typedef void (*ObngStereoClearEyesFn)(void);

#ifdef __cplusplus
}  // extern "C"
#endif
