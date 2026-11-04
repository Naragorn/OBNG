#pragma once

// The eye switch's decisions over plain values, so every flow runs in a test
// without the game (plugin/tests/StereoEyeSwitchLogicTest.cpp): the view's
// shifted translation rows for the second eye, the check of what the C
// interface (StereoEyesApi.h) hands in, the plugin's own test eye data (the
// current viewport split in halves), the plan that follows from the settings
// and the data, and the counters. RouteStereoDraw.cpp supplies the engine
// ports: the renderer's matrices, the device and the constants re-apply.
//
// What the switch changes, and why only that (docs/subsystems/
// single-pass-stereo.md, step 1): the two eyes differ by a translation of
// the camera; the renderer draws camera-relative against one origin
// (0x00B3F92C, kept), so an eye's position enters the view matrix +0x980
// through its translation row alone - `_4k = -(axis_k . T)` with T the
// view's origin (re/functions/0x00762640.md C2) - and the inverse view
// +0xA00 through its row 4 (C3: (T, 1)). Moving T by the eye offset e
// (world space) gives `_4k - axis_k . e` and `T + e`: ShiftedRows. The
// axes are the columns of +0x980 (C2). Derived from C2 and C3; the
// orthonormality the inverse rests on is the original's own assumption.

#include "StereoEyesApi.h"
#include "engine/render/StereoDraw.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace obng::plugin::stereo_eye_switch {

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	constexpr bool operator==(const Vec3&) const = default;
};

constexpr float Dot(Vec3 a, Vec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 Scaled(Vec3 v, float s) {
	return Vec3{v.x * s, v.y * s, v.z * s};
}

// D3DVIEWPORT9, field for field (StereoEyesApi.h ObngStereoViewport).
struct Viewport {
	std::uint32_t x = 0;
	std::uint32_t y = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	float minZ = 0.0f;
	float maxZ = 1.0f;
	constexpr bool operator==(const Viewport&) const = default;
};
static_assert(sizeof(Viewport) == 24, "D3DVIEWPORT9 is 24 bytes");
static_assert(offsetof(Viewport, minZ) == 16, "MinZ after the four dwords");

// The eye data the switch works from: eye 0 is the pass camera (offset
// zero, by the contract of StereoEyesApi.h), eye 1 the camera moved by the
// offset.
struct EyeData {
	Vec3 secondEyeOffset;
	Viewport viewport[2];
	constexpr bool operator==(const EyeData&) const = default;
};

// The columns of the view matrix +0x980 (0x00762640.md C2): right = _11,
// _21, _31; up = _12, _22, _32; direction = _13, _23, _33.
struct ViewAxes {
	Vec3 right;
	Vec3 up;
	Vec3 direction;
};

// Row 4 of the view (+0x980 + 0x30) and of the inverse view (+0xA00 + 0x30),
// the three translation cells each; _44 stays.
struct TranslationRows {
	float view[3] = {0.0f, 0.0f, 0.0f};
	float inverse[3] = {0.0f, 0.0f, 0.0f};
	constexpr bool operator==(const TranslationRows&) const = default;
};

// The rows for a camera moved by offsetWorld from the one the base rows
// describe (derived from C2, C3 as above).
constexpr TranslationRows ShiftedRows(const TranslationRows& base, const ViewAxes& axes, Vec3 offsetWorld) {
	TranslationRows out;
	out.view[0] = base.view[0] - Dot(axes.right, offsetWorld);
	out.view[1] = base.view[1] - Dot(axes.up, offsetWorld);
	out.view[2] = base.view[2] - Dot(axes.direction, offsetWorld);
	out.inverse[0] = base.inverse[0] + offsetWorld.x;
	out.inverse[1] = base.inverse[1] + offsetWorld.y;
	out.inverse[2] = base.inverse[2] + offsetWorld.z;
	return out;
}

// The 16 floats of a D3DMATRIX in row order: the axes from the columns, the
// translation row from cells 12..14.
constexpr ViewAxes AxesOf(const float* viewMatrix) {
	return ViewAxes{Vec3{viewMatrix[0], viewMatrix[4], viewMatrix[8]}, Vec3{viewMatrix[1], viewMatrix[5], viewMatrix[9]},
	                Vec3{viewMatrix[2], viewMatrix[6], viewMatrix[10]}};
}

constexpr TranslationRows RowsOf(const float* viewMatrix, const float* inverseViewMatrix) {
	TranslationRows rows;
	for (int k = 0; k < 3; ++k) {
		rows.view[k] = viewMatrix[12 + k];
		rows.inverse[k] = inverseViewMatrix[12 + k];
	}
	return rows;
}

constexpr void WriteRows(const TranslationRows& rows, float* viewMatrix, float* inverseViewMatrix) {
	for (int k = 0; k < 3; ++k) {
		viewMatrix[12 + k] = rows.view[k];
		inverseViewMatrix[12 + k] = rows.inverse[k];
	}
}

// ---- The C interface's input -------------------------------------------------

constexpr bool ViewportUsable(const ObngStereoViewport& v) {
	return v.width != 0 && v.height != 0 && v.minZ >= 0.0f && v.maxZ <= 1.0f && v.minZ <= v.maxZ;
}

inline bool Finite(float f) {
	return std::isfinite(f);
}

// The verdict on what ObngStereoSetEyes received, in the order the caller
// can act on: pointer, layout, content, then the route's own state.
inline ObngStereoResult Validate(const ObngStereoEyes* eyes, std::uint32_t plan) {
	if (eyes == nullptr) {
		return kObngStereoNull;
	}
	if (eyes->size != sizeof(ObngStereoEyes)) {
		return kObngStereoSize;
	}
	if (eyes->version != kObngStereoEyesVersion) {
		return kObngStereoVersion;
	}
	if (!ViewportUsable(eyes->viewport[0]) || !ViewportUsable(eyes->viewport[1])) {
		return kObngStereoViewport;
	}
	if (!Finite(eyes->secondEyeOffset[0]) || !Finite(eyes->secondEyeOffset[1]) || !Finite(eyes->secondEyeOffset[2])) {
		return kObngStereoOffset;
	}
	if (plan != 2) {
		return kObngStereoRouteOff;
	}
	return kObngStereoOk;
}

constexpr Viewport FromApi(const ObngStereoViewport& v) {
	return Viewport{v.x, v.y, v.width, v.height, v.minZ, v.maxZ};
}

constexpr EyeData FromApi(const ObngStereoEyes& eyes) {
	return EyeData{Vec3{eyes.secondEyeOffset[0], eyes.secondEyeOffset[1], eyes.secondEyeOffset[2]},
	               {FromApi(eyes.viewport[0]), FromApi(eyes.viewport[1])}};
}

// ---- The plugin's own test eye data ([Route] StereoTestEyeShift) ------------
//
// Without OBVR's double-wide target the switch can still be run and costed:
// the viewport the engine set is split in two halves (eye 0 left, eye 1
// right, an odd pixel to the right), and eye 1 sits eyeShift game units
// along the camera's right axis. The picture is two squeezed half-views
// with the eye parallax between them - a sanity check, not a VR frame.

constexpr bool TestModeOn(float eyeShift) {
	return eyeShift != 0.0f;
}

constexpr EyeData SplitHalves(const Viewport& whole, float eyeShift, const ViewAxes& axes) {
	const std::uint32_t left = whole.width / 2;
	EyeData data;
	data.secondEyeOffset = Scaled(axes.right, eyeShift);
	data.viewport[0] = Viewport{whole.x, whole.y, left, whole.height, whole.minZ, whole.maxZ};
	data.viewport[1] = Viewport{whole.x + left, whole.y, whole.width - left, whole.height, whole.minZ, whole.maxZ};
	return data;
}

// ---- The plan per draw ---------------------------------------------------------
//
// [Route] StereoEyes=2 allows the double draw; it happens only while eye
// data exists (set through the interface, or the test mode), so that with
// nothing to switch to the draws stay single - a second draw into the same
// viewport with the same view is a wasted draw at best and a double alpha
// blend at worst.
constexpr engine::render::stereo_draw::EyePlan EffectivePlan(std::uint8_t allowedEyes, bool hasEyeData) {
	return engine::render::stereo_draw::EyePlan{static_cast<std::uint8_t>(allowedEyes == 2 && hasEyeData ? 2 : 1)};
}

// What ObngStereoPlan reports.
constexpr std::uint32_t ReportedPlan(bool routeOn, std::uint8_t allowedEyes) {
	return routeOn ? allowedEyes : 0u;
}

// ---- What one switch does, as a record the tests can read -----------------
//
// The engine switch (RouteStereoDraw.cpp) follows these steps in this order
// for Switch(1): read the axes and the base rows, write the shifted rows,
// SetTransform(VIEW), SetViewport(eye 1), re-apply the constants (shader
// slot 14 again with the partition's own arguments); for Switch(0): write
// the base rows back, SetTransform(VIEW), SetViewport(eye 0). The logic
// below drives a port with those steps so that the order and the values are
// tested once, here, and the engine port only forwards.

struct SwitchCounters {
	std::uint64_t applied = 0;   // Switch(1) with eye data: the second eye's state set
	std::uint64_t restored = 0;  // Switch(0) after an applied switch
	std::uint64_t noData = 0;    // Switch(1) without eye data: nothing done
};

template <class Port>
struct EyeSwitch {
	Port* port;  // ReadView(float out[16]) / ReadInverseRow(float out[3]) / WriteRows(rows) / SetViewTransform() / SetViewport(vp) / ReapplyConstants() / CurrentViewport() -> Viewport
	SwitchCounters* counters;
	const EyeData* eyes;   // null: no data
	float testEyeShift;    // 0: no test mode

	TranslationRows saved{};
	EyeData current{};
	bool applied = false;

	void Switch(std::uint8_t eye) {
		if (eye == 1) {
			SwitchToSecond();
		} else {
			SwitchBack();
		}
	}

	void SwitchToSecond() {
		const bool testMode = TestModeOn(testEyeShift);
		if (eyes == nullptr && !testMode) {
			++counters->noData;
			applied = false;
			return;
		}
		float view[16];
		float inverseRow[3];
		port->ReadView(view);
		port->ReadInverseRow(inverseRow);
		const ViewAxes axes = AxesOf(view);
		saved = TranslationRows{{view[12], view[13], view[14]}, {inverseRow[0], inverseRow[1], inverseRow[2]}};
		current = eyes != nullptr ? *eyes : SplitHalves(port->CurrentViewport(), testEyeShift, axes);
		port->WriteRows(ShiftedRows(saved, axes, current.secondEyeOffset));
		port->SetViewTransform();
		port->SetViewport(current.viewport[1]);
		port->ReapplyConstants();
		applied = true;
		++counters->applied;
	}

	void SwitchBack() {
		if (!applied) {
			return;
		}
		port->WriteRows(saved);
		port->SetViewTransform();
		port->SetViewport(current.viewport[0]);
		applied = false;
		++counters->restored;
	}
};

}  // namespace obng::plugin::stereo_eye_switch
