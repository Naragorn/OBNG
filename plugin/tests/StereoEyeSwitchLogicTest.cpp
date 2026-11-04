// The eye switch's decisions (plugin/src/StereoEyeSwitchLogic.h): the shifted
// translation rows, the interface's validation, the test mode's halves, the
// plan per draw, and the switch's step order over a recording port.

#include "Check.h"
#include "StereoEyeSwitchLogic.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace sw = obng::plugin::stereo_eye_switch;
using sw::EyeData;
using sw::TranslationRows;
using sw::Vec3;
using sw::ViewAxes;
using sw::Viewport;

namespace {

ObngStereoEyes GoodEyes() {
	ObngStereoEyes e{};
	e.size = sizeof(ObngStereoEyes);
	e.version = kObngStereoEyesVersion;
	e.secondEyeOffset[0] = 1.0f;
	e.secondEyeOffset[1] = 2.0f;
	e.secondEyeOffset[2] = 3.0f;
	e.viewport[0] = ObngStereoViewport{0, 0, 960, 1080, 0.0f, 1.0f};
	e.viewport[1] = ObngStereoViewport{960, 0, 960, 1080, 0.0f, 1.0f};
	return e;
}

// The port the switch drives, recording every step in order.
struct RecordingPort {
	float view[16] = {};
	float inverse[16] = {};
	Viewport viewport{10, 20, 640, 480, 0.0f, 1.0f};
	std::vector<std::string> steps;
	std::vector<TranslationRows> rowsWritten;
	std::vector<Viewport> viewportsSet;

	void ReadView(float out[16]) {
		steps.push_back("read view");
		std::memcpy(out, view, sizeof(view));
	}
	void ReadInverseRow(float out[3]) {
		steps.push_back("read inverse row");
		out[0] = inverse[12];
		out[1] = inverse[13];
		out[2] = inverse[14];
	}
	void WriteRows(const TranslationRows& rows) {
		steps.push_back("write rows");
		rowsWritten.push_back(rows);
		sw::WriteRows(rows, view, inverse);
	}
	void SetViewTransform() { steps.push_back("set view transform"); }
	void SetViewport(const Viewport& vp) {
		steps.push_back("set viewport");
		viewportsSet.push_back(vp);
	}
	void ReapplyConstants() { steps.push_back("reapply constants"); }
	Viewport CurrentViewport() {
		steps.push_back("get viewport");
		return viewport;
	}
};

bool StepsAre(const RecordingPort& p, std::initializer_list<const char*> expected) {
	if (p.steps.size() != expected.size()) {
		return false;
	}
	std::size_t i = 0;
	for (const char* e : expected) {
		if (p.steps[i++] != e) {
			return false;
		}
	}
	return true;
}

// A camera looking along world +X with up +Z, right +Y... as columns of the view.
void IdentityView(float* m) {
	for (int i = 0; i < 16; ++i) {
		m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	}
}

}  // namespace

int main() {
	Section("ShiftedRows: with identity axes the view row moves by -e, the inverse row by +e");
	{
		const ViewAxes axes{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
		const TranslationRows base{{0.5f, -1.0f, 2.0f}, {7.0f, 8.0f, 9.0f}};
		const TranslationRows out = sw::ShiftedRows(base, axes, Vec3{1.0f, 2.0f, 3.0f});
		Check(out.view[0] == -0.5f && out.view[1] == -3.0f && out.view[2] == -1.0f, "view row = base - e");
		Check(out.inverse[0] == 8.0f && out.inverse[1] == 10.0f && out.inverse[2] == 12.0f, "inverse row = base + e");
		Check(sw::ShiftedRows(base, axes, Vec3{}) == base, "a zero offset leaves both rows");
	}

	Section("ShiftedRows: rotated axes project the world offset onto each axis");
	{
		// right = world +Y, up = world +Z, direction = world +X (Oblivion's camera looks along its local X).
		const ViewAxes axes{Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}};
		const TranslationRows base{};
		const TranslationRows out = sw::ShiftedRows(base, axes, Vec3{0.0f, 4.5f, 0.0f});
		Check(out.view[0] == -4.5f && out.view[1] == 0.0f && out.view[2] == 0.0f, "an offset along the right axis lands in _41 only");
		Check(out.inverse[1] == 4.5f && out.inverse[0] == 0.0f && out.inverse[2] == 0.0f, "the inverse row carries the world vector");
	}

	Section("AxesOf / RowsOf / WriteRows read and write the matrix cells the map names");
	{
		float view[16];
		float inverse[16];
		for (int i = 0; i < 16; ++i) {
			view[i] = static_cast<float>(i);
			inverse[i] = 100.0f + static_cast<float>(i);
		}
		const ViewAxes axes = sw::AxesOf(view);
		Check(axes.right == Vec3{0, 4, 8} && axes.up == Vec3{1, 5, 9} && axes.direction == Vec3{2, 6, 10}, "the columns");
		const TranslationRows rows = sw::RowsOf(view, inverse);
		Check(rows.view[0] == 12 && rows.view[2] == 14 && rows.inverse[0] == 112 && rows.inverse[2] == 114, "row 4, three cells each");
		sw::WriteRows(TranslationRows{{-1, -2, -3}, {-4, -5, -6}}, view, inverse);
		Check(view[12] == -1 && view[14] == -3 && view[15] == 15 && inverse[13] == -5 && inverse[15] == 115, "cells 12..14 written, _44 untouched");
	}

	Section("Validate: each refusal in order, then ok");
	{
		Check(sw::Validate(nullptr, 2) == kObngStereoNull, "null");
		ObngStereoEyes e = GoodEyes();
		e.size = 4;
		Check(sw::Validate(&e, 2) == kObngStereoSize, "wrong size");
		e = GoodEyes();
		e.version = 2;
		Check(sw::Validate(&e, 2) == kObngStereoVersion, "wrong version");
		e = GoodEyes();
		e.viewport[1].width = 0;
		Check(sw::Validate(&e, 2) == kObngStereoViewport, "zero width");
		e = GoodEyes();
		e.viewport[0].height = 0;
		Check(sw::Validate(&e, 2) == kObngStereoViewport, "zero height");
		e = GoodEyes();
		e.viewport[0].minZ = 0.7f;
		e.viewport[0].maxZ = 0.3f;
		Check(sw::Validate(&e, 2) == kObngStereoViewport, "inverted depth range");
		e = GoodEyes();
		e.viewport[1].maxZ = 1.5f;
		Check(sw::Validate(&e, 2) == kObngStereoViewport, "MaxZ above 1");
		e = GoodEyes();
		e.secondEyeOffset[2] = std::numeric_limits<float>::quiet_NaN();
		Check(sw::Validate(&e, 2) == kObngStereoOffset, "a NaN offset");
		e = GoodEyes();
		e.secondEyeOffset[0] = std::numeric_limits<float>::infinity();
		Check(sw::Validate(&e, 2) == kObngStereoOffset, "an infinite offset");
		e = GoodEyes();
		Check(sw::Validate(&e, 1) == kObngStereoRouteOff, "route on with one eye: refused");
		Check(sw::Validate(&e, 0) == kObngStereoRouteOff, "route off: refused");
		Check(sw::Validate(&e, 2) == kObngStereoOk, "good data with two eyes allowed: ok");
		const EyeData d = sw::FromApi(e);
		Check(d.secondEyeOffset == Vec3{1, 2, 3} && d.viewport[0] == Viewport{0, 0, 960, 1080, 0.0f, 1.0f} &&
		          d.viewport[1] == Viewport{960, 0, 960, 1080, 0.0f, 1.0f},
		      "FromApi copies field for field");
	}

	Section("SplitHalves: left and right halves, the odd pixel to the right, eye 1 along the right axis");
	{
		const ViewAxes axes{Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}};
		const EyeData d = sw::SplitHalves(Viewport{10, 20, 641, 480, 0.1f, 0.9f}, 4.5f, axes);
		Check(d.viewport[0] == Viewport{10, 20, 320, 480, 0.1f, 0.9f}, "eye 0: the left half");
		Check(d.viewport[1] == Viewport{330, 20, 321, 480, 0.1f, 0.9f}, "eye 1: the right half with the odd pixel");
		Check(d.secondEyeOffset == Vec3{0, 4.5f, 0}, "the shift along the camera's right axis in world space");
		Check(!sw::TestModeOn(0.0f) && sw::TestModeOn(4.5f) && sw::TestModeOn(-1.0f), "the test mode is any non-zero shift");
	}

	Section("ViewReapplyTargets: the maps with the vertex vtable and a program, in the original's order");
	{
		const std::uint32_t v = sw::kNiD3DSCMVertexVtable;
		const sw::ReapplyTargets both = sw::ViewReapplyTargets(0x100, 0x200, v, 0x300, 0x400, v);
		Check(both.count == 2 && both.target[0] == sw::ReapplyTarget{0x100, 0x200} && both.target[1] == sw::ReapplyTarget{0x300, 0x400},
		      "two vertex maps: both, A first");
		const sw::ReapplyTargets pixelB = sw::ViewReapplyTargets(0x100, 0x200, v, 0x300, 0x400, 0x00AB297C);
		Check(pixelB.count == 1 && pixelB.target[0] == sw::ReapplyTarget{0x100, 0x200}, "B with the pixel vtable: A only");
		Check(sw::ViewReapplyTargets(0, 0x200, v, 0x300, 0x400, v).count == 1, "a null map A: B only");
		Check(sw::ViewReapplyTargets(0x100, 0, v, 0x300, 0x400, v).target[0] == sw::ReapplyTarget{0x300, 0x400}, "a null program A: B only");
		Check(sw::ViewReapplyTargets(0x100, 0x200, 0x00AB2A4C, 0, 0, 0).count == 0, "the base vtable: nothing");
	}

	Section("EffectivePlan and ReportedPlan");
	{
		Check(sw::EffectivePlan(2, true).eyes == 2, "two allowed with data: two");
		Check(sw::EffectivePlan(2, false).eyes == 1, "two allowed without data: one");
		Check(sw::EffectivePlan(1, true).eyes == 1, "one allowed with data: one");
		Check(sw::ReportedPlan(false, 2) == 0 && sw::ReportedPlan(true, 1) == 1 && sw::ReportedPlan(true, 2) == 2, "0 off, else the allowed eyes");
	}

	Section("EyeSwitch with interface data: the step order, the rows, the viewports, the counters");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		port.view[12] = 0.25f;
		port.inverse[13] = -2.0f;
		sw::SwitchCounters counters;
		const EyeData eyes = sw::FromApi(GoodEyes());
		sw::EyeSwitch<RecordingPort> s{&port, &counters, &eyes, 0.0f};
		s.Switch(1);
		Check(StepsAre(port, {"read view", "read inverse row", "write rows", "set view transform", "set viewport", "reapply constants"}),
		      "Switch(1): read, write the shifted rows, VIEW, viewport, constants");
		Check(port.rowsWritten.size() == 1 && port.rowsWritten[0] == TranslationRows{{0.25f - 1.0f, -2.0f, -3.0f}, {1.0f, 0.0f, 3.0f}},
		      "the shifted rows from the base rows and the offset");
		Check(port.viewportsSet.size() == 1 && port.viewportsSet[0] == eyes.viewport[1], "eye 1's viewport");
		port.steps.clear();
		s.Switch(0);
		Check(StepsAre(port, {"write rows", "set view transform", "set viewport"}), "Switch(0): the base rows back, VIEW, eye 0's viewport");
		Check(port.rowsWritten.size() == 2 && port.rowsWritten[1] == TranslationRows{{0.25f, 0.0f, 0.0f}, {0.0f, -2.0f, 0.0f}},
		      "the base rows bit for bit");
		Check(port.view[12] == 0.25f && port.view[13] == 0.0f && port.inverse[13] == -2.0f, "the matrices are as before");
		Check(port.viewportsSet.size() == 2 && port.viewportsSet[1] == eyes.viewport[0], "eye 0's viewport");
		Check(counters.applied == 1 && counters.restored == 1 && counters.noData == 0, "one applied, one restored");
	}

	Section("EyeSwitch without data: Switch(1) counts and touches nothing, Switch(0) does nothing");
	{
		RecordingPort port;
		sw::SwitchCounters counters;
		sw::EyeSwitch<RecordingPort> s{&port, &counters, nullptr, 0.0f};
		s.Switch(1);
		s.Switch(0);
		Check(port.steps.empty(), "no port call");
		Check(counters.noData == 1 && counters.applied == 0 && counters.restored == 0, "counted as no data");
	}

	Section("EyeSwitch in test mode: the viewport is read and split, the shift along the right axis");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		port.viewport = Viewport{0, 0, 1000, 500, 0.0f, 1.0f};
		sw::SwitchCounters counters;
		sw::EyeSwitch<RecordingPort> s{&port, &counters, nullptr, 4.5f};
		s.Switch(1);
		Check(StepsAre(port, {"read view", "read inverse row", "get viewport", "write rows", "set view transform", "set viewport",
		                      "reapply constants"}),
		      "the current viewport is read before the rows are written");
		Check(port.viewportsSet.size() == 1 && port.viewportsSet[0] == Viewport{500, 0, 500, 500, 0.0f, 1.0f}, "eye 1: the right half");
		Check(port.rowsWritten[0] == TranslationRows{{-4.5f, 0.0f, 0.0f}, {4.5f, 0.0f, 0.0f}}, "right = world X with identity axes");
		s.Switch(0);
		Check(port.viewportsSet.size() == 2 && port.viewportsSet[1] == Viewport{0, 0, 500, 500, 0.0f, 1.0f}, "eye 0: the left half");
		Check(counters.applied == 1 && counters.restored == 1, "counted");
	}

	Section("EyeSwitch: interface data wins over the test mode when both exist");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		sw::SwitchCounters counters;
		const EyeData eyes = sw::FromApi(GoodEyes());
		sw::EyeSwitch<RecordingPort> s{&port, &counters, &eyes, 4.5f};
		s.Switch(1);
		Check(port.viewportsSet[0] == eyes.viewport[1] && !StepsAre(port, {"get viewport"}), "no viewport read, the interface's viewport set");
		bool readViewport = false;
		for (const std::string& step : port.steps) {
			readViewport = readViewport || step == "get viewport";
		}
		Check(!readViewport, "the current viewport is not read");
	}

	return Failures();
}
