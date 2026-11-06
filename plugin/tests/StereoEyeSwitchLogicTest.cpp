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
	// The device's vertex registers, and what a re-apply does to them: the
	// registers in reapplyWrites get the view's translation row cell 12 in
	// their fourth float, as a matrix the engine uploads again would.
	float registers[sw::kCarriedRegisters * 4] = {};
	std::vector<int> reapplyWrites;
	bool registersReadable = true;
	std::vector<std::string> registerWrites;
	void ReapplyConstants() {
		steps.push_back("reapply constants");
		for (const int r : reapplyWrites) {
			registers[r * 4 + 3] = 1000.0f + view[12] + static_cast<float>(r);
		}
	}
	bool ReadRegisters(float* out) {
		steps.push_back("read registers");
		if (!registersReadable) {
			return false;
		}
		std::memcpy(out, registers, sizeof(registers));
		return true;
	}
	void WriteRegisters(std::uint32_t start, const float* data, std::uint32_t count) {
		steps.push_back("write registers");
		registerWrites.push_back("c" + std::to_string(start) + "x" + std::to_string(count));
		std::memcpy(registers + start * 4, data, count * 4 * sizeof(float));
	}
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

	Section("The viewport gate: eye 0's rectangle, the depth range not compared");
	{
		const EyeData eyes = sw::FromApi(GoodEyes());
		Check(sw::InEyeViewport(Viewport{0, 0, 960, 1080, 0.0f, 1.0f}, eyes), "the world pass's viewport: in");
		Check(sw::InEyeViewport(Viewport{0, 0, 960, 1080, 0.2f, 0.8f}, eyes), "another depth range: still in");
		Check(!sw::InEyeViewport(Viewport{0, 0, 1024, 1024, 0.0f, 1.0f}, eyes), "a shadow map's viewport: out");
		Check(!sw::InEyeViewport(Viewport{960, 0, 960, 1080, 0.0f, 1.0f}, eyes), "eye 1's own rectangle: out");
		Check(!sw::InEyeViewport(Viewport{0, 0, 1920, 1080, 0.0f, 1.0f}, eyes), "the whole target: out");
	}

	Section("MapsSourceFor: SpeedTree branches take the type-1 shader's maps, everyone else their own");
	{
		Check(sw::MapsSourceFor(sw::kSpeedTreeBranchShaderVtable) == sw::MapsSource::kType1Shader, "SpeedTreeBranchShader");
		Check(sw::MapsSourceFor(0x00A8977C) == sw::MapsSource::kOwn, "NiD3DShader");
		Check(sw::MapsSourceFor(0) == sw::MapsSource::kOwn, "nothing");
		sw::ShaderClasses classes;
		classes.Add(0x10);
		classes.Add(0x20);
		classes.Add(0x10);
		Check(classes.size == 2 && classes.rows[0].count == 2 && classes.rows[1].vtable == 0x20, "two classes, counted");
		for (std::uint32_t i = 0; i < sw::ShaderClasses::kMaxRows + 3; ++i) {
			classes.Add(0x1000 + i);
		}
		Check(classes.size == sw::ShaderClasses::kMaxRows && classes.overflow == 5, "classes beyond the table are counted");
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

	Section("EyeSwitch with the constants restored: Switch(0) re-applies them for the first eye after the rows are back");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		sw::SwitchCounters counters;
		const EyeData eyes = sw::FromApi(GoodEyes());
		sw::EyeSwitch<RecordingPort> s{&port, &counters, &eyes, 0.0f};
		s.reapplyOnRestore = true;
		s.Switch(1);
		port.steps.clear();
		s.Switch(0);
		Check(StepsAre(port, {"write rows", "set view transform", "set viewport", "reapply constants"}),
		      "Switch(0): the base rows, VIEW, eye 0's viewport, then the constants again");
		Check(counters.applied == 1 && counters.restored == 1, "counted as before");
		sw::EyeSwitch<RecordingPort> follower{&port, &counters, &eyes, 0.0f};
		follower.reapplyOnRestore = true;
		follower.viewportOnly = true;
		follower.Switch(1);
		port.steps.clear();
		follower.Switch(0);
		Check(StepsAre(port, {"set viewport"}), "a camera follower's constants were never changed: nothing to restore");
	}

	Section("EyeSwitch for a camera follower (the sky): the second eye gets its viewport and nothing else");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		port.view[12] = 0.25f;
		sw::SwitchCounters counters;
		const EyeData eyes = sw::FromApi(GoodEyes());
		sw::EyeSwitch<RecordingPort> s{&port, &counters, &eyes, 0.0f};
		s.viewportOnly = true;
		s.Switch(1);
		Check(StepsAre(port, {"set viewport"}), "Switch(1): eye 1's viewport only - no rows, no VIEW, no constants");
		Check(port.viewportsSet.size() == 1 && port.viewportsSet[0] == eyes.viewport[1] && port.rowsWritten.empty(), "eye 1's viewport, the view untouched");
		port.steps.clear();
		s.Switch(0);
		Check(StepsAre(port, {"set viewport"}), "Switch(0): eye 0's viewport only");
		Check(port.viewportsSet.size() == 2 && port.viewportsSet[1] == eyes.viewport[0] && port.view[12] == 0.25f, "eye 0's viewport, the view as it was");
		Check(counters.applied == 1 && counters.restored == 1 && counters.viewportOnly == 1 && counters.noData == 0,
		      "applied and restored, counted as viewport-only");
		port.steps.clear();
		s.Switch(0);
		Check(port.steps.empty() && counters.restored == 1, "a second Switch(0) does nothing");
	}

	Section("EyeSwitch for a camera follower in test mode: the halves of the current viewport, no shift");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		port.viewport = Viewport{0, 0, 1000, 500, 0.0f, 1.0f};
		sw::SwitchCounters counters;
		sw::EyeSwitch<RecordingPort> s{&port, &counters, nullptr, 4.5f};
		s.viewportOnly = true;
		s.Switch(1);
		Check(StepsAre(port, {"read view", "get viewport", "set viewport"}), "the viewport is read and split; nothing is written");
		Check(port.viewportsSet[0] == Viewport{500, 0, 500, 500, 0.0f, 1.0f} && port.rowsWritten.empty(), "eye 1: the right half");
		s.Switch(0);
		Check(port.viewportsSet.size() == 2 && port.viewportsSet[1] == Viewport{0, 0, 500, 500, 0.0f, 1.0f}, "eye 0: the left half");
		Check(counters.viewportOnly == 1 && counters.applied == 1 && counters.restored == 1, "counted");
	}

	Section("EyeSwitch for a camera follower without data: nothing done");
	{
		RecordingPort port;
		sw::SwitchCounters counters;
		sw::EyeSwitch<RecordingPort> s{&port, &counters, nullptr, 0.0f};
		s.viewportOnly = true;
		s.Switch(1);
		s.Switch(0);
		Check(port.steps.empty() && counters.noData == 1 && counters.applied == 0 && counters.viewportOnly == 0, "counted as no data");
	}

	Section("Carried registers: the re-apply's pair is learnt, and the switch back restores the registers by value");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		port.reapplyWrites = {0};
		for (int i = 0; i < sw::kCarriedRegisters * 4; ++i) {
			port.registers[i] = static_cast<float>(i);
		}
		float original[sw::kCarriedRegisters * 4];
		std::memcpy(original, port.registers, sizeof(original));
		const EyeData eyes = sw::FromApi(GoodEyes());
		sw::SwitchCounters counters;
		sw::RegisterPairs pairs;
		sw::EyeSwitch<RecordingPort> s{&port, &counters, &eyes, 0.0f};
		s.pairs = &pairs;
		s.reapplyOnRestore = true;  // superseded: the restore is by value
		s.Switch(1);
		Check(StepsAre(port, {"read view", "read inverse row", "read registers", "write rows", "set view transform", "set viewport",
		                      "reapply constants", "read registers"}),
		      "the registers are read before the rows move and after the re-apply");
		Check(port.registers[3] == 1000.0f - 1.0f, "the second eye draws with the re-apply's c0 (the view row moved by -1)");
		Check(pairs.valid == 1u && pairs.first[3] == 3.0f && pairs.second[3] == 999.0f && pairs.offset == eyes.secondEyeOffset,
		      "the pair of c0 is remembered with the offset it was made for");
		Check(port.registerWrites.empty() && counters.carriedRegisters == 0, "nothing carried: the re-apply did it");
		port.steps.clear();
		s.Switch(0);
		Check(StepsAre(port, {"write rows", "set view transform", "set viewport", "write registers"}),
		      "the switch back writes the registers and does not call the re-apply");
		Check(port.registerWrites == std::vector<std::string>{"c0x1"}, "only the changed register is written");
		Check(std::memcmp(port.registers, original, sizeof(original)) == 0, "the device's registers are what they were before the switch");
		Check(!s.carried && s.touched == 0, "the switch's state is cleared");

		Section("Carried registers: a later draw whose re-apply leaves c0 alone gets the remembered second-eye value");
		port.reapplyWrites.clear();  // this pass's map has no entry for c0
		port.steps.clear();
		port.registerWrites.clear();
		sw::EyeSwitch<RecordingPort> later{&port, &counters, &eyes, 0.0f};
		later.pairs = &pairs;
		later.Switch(1);
		Check(port.registers[3] == 999.0f && port.registers[0] == 0.0f, "c0 holds the second eye's remembered value, whole register");
		Check(port.registerWrites == std::vector<std::string>{"c0x1"} && counters.carriedRegisters == 1, "one register carried, counted");
		later.Switch(0);
		Check(std::memcmp(port.registers, original, sizeof(original)) == 0, "and the first eye's value is back after the draw");
		Check(port.registerWrites == std::vector<std::string>{"c0x1", "c0x1"}, "one write each way");

		Section("Carried registers: a register that holds something else is left alone and forgotten");
		port.registers[3] = 77.0f;  // someone uploaded another c0
		port.registerWrites.clear();
		sw::EyeSwitch<RecordingPort> other{&port, &counters, &eyes, 0.0f};
		other.pairs = &pairs;
		other.Switch(1);
		Check(port.registers[3] == 77.0f && port.registerWrites.empty() && pairs.valid == 0, "no write, the pair dropped");
		other.Switch(0);
		Check(port.registerWrites.empty(), "nothing to restore");
		port.registers[3] = 3.0f;  // the old first-eye value again, by chance
		sw::EyeSwitch<RecordingPort> again{&port, &counters, &eyes, 0.0f};
		again.pairs = &pairs;
		again.Switch(1);
		Check(port.registers[3] == 3.0f && port.registerWrites.empty(), "a dropped pair does not come back");
		again.Switch(0);
	}

	Section("Carried registers: runs are written whole, a learnt and a carried register in one switch");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		const EyeData eyes = sw::FromApi(GoodEyes());
		sw::SwitchCounters counters;
		sw::RegisterPairs pairs;
		port.reapplyWrites = {0, 1, 2, 3, 9};
		sw::EyeSwitch<RecordingPort> first{&port, &counters, &eyes, 0.0f};
		first.pairs = &pairs;
		first.Switch(1);
		first.Switch(0);
		Check(port.registerWrites == std::vector<std::string>{"c0x4", "c9x1"}, "the restore writes c0..c3 as one run and c9 alone");
		Check(pairs.valid == 0x20Fu, "five pairs");
		port.reapplyWrites = {9};  // the next pass uploads c9 itself and relies on c0..c3
		port.registerWrites.clear();
		sw::EyeSwitch<RecordingPort> second{&port, &counters, &eyes, 0.0f};
		second.pairs = &pairs;
		second.Switch(1);
		Check(port.registerWrites == std::vector<std::string>{"c0x4"} && counters.carriedRegisters == 4, "c0..c3 carried as one run");
		Check(port.registers[3] == 999.0f && port.registers[9 * 4 + 3] == 1008.0f, "both the carried and the re-applied register are the second eye's");
		second.Switch(0);
		Check(port.registerWrites == std::vector<std::string>{"c0x4", "c0x4", "c9x1"}, "the restore covers the carried and the re-applied");
		Check(port.registers[3] == 0.0f && port.registers[9 * 4 + 3] == 0.0f, "all back");
	}

	Section("Carried registers: another eye offset empties the cache");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		EyeData eyes = sw::FromApi(GoodEyes());
		sw::SwitchCounters counters;
		sw::RegisterPairs pairs;
		port.reapplyWrites = {0};
		sw::EyeSwitch<RecordingPort> first{&port, &counters, &eyes, 0.0f};
		first.pairs = &pairs;
		first.Switch(1);
		first.Switch(0);
		eyes.secondEyeOffset.x = 5.0f;
		port.reapplyWrites.clear();
		port.registerWrites.clear();
		sw::EyeSwitch<RecordingPort> second{&port, &counters, &eyes, 0.0f};
		second.pairs = &pairs;
		second.Switch(1);
		Check(port.registerWrites.empty() && pairs.valid == 0 && pairs.offset.x == 5.0f, "the pair made for the old offset is not used");
		second.Switch(0);
	}

	Section("Carried registers: a device that does not give its registers leaves the switch as it was");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		port.registersReadable = false;
		port.reapplyWrites = {0};
		const EyeData eyes = sw::FromApi(GoodEyes());
		sw::SwitchCounters counters;
		sw::RegisterPairs pairs;
		sw::EyeSwitch<RecordingPort> s{&port, &counters, &eyes, 0.0f};
		s.pairs = &pairs;
		s.reapplyOnRestore = true;
		s.Switch(1);
		Check(StepsAre(port, {"read view", "read inverse row", "read registers", "write rows", "set view transform", "set viewport",
		                      "reapply constants"}),
		      "one read tried, no second");
		port.steps.clear();
		s.Switch(0);
		Check(StepsAre(port, {"write rows", "set view transform", "set viewport", "reapply constants"}) && port.registerWrites.empty() &&
		          pairs.valid == 0,
		      "the switch back re-applies as without pairs");
	}

	Section("Carried registers: the read after the re-apply failing drops the carry for this switch");
	{
		struct FailingSecondRead : RecordingPort {
			int reads = 0;
			bool ReadRegisters(float* out) {
				++reads;
				registersReadable = reads == 1;
				return RecordingPort::ReadRegisters(out);
			}
		};
		FailingSecondRead port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		port.reapplyWrites = {0};
		const EyeData eyes = sw::FromApi(GoodEyes());
		sw::SwitchCounters counters;
		sw::RegisterPairs pairs;
		sw::EyeSwitch<FailingSecondRead> s{&port, &counters, &eyes, 0.0f};
		s.pairs = &pairs;
		s.reapplyOnRestore = true;
		s.Switch(1);
		port.steps.clear();
		s.Switch(0);
		Check(pairs.valid == 0 && port.registerWrites.empty() && port.steps.back() == "reapply constants", "restored by the re-apply");
	}

	Section("Carried registers: a camera follower and a switch without data read no register");
	{
		RecordingPort port;
		IdentityView(port.view);
		IdentityView(port.inverse);
		const EyeData eyes = sw::FromApi(GoodEyes());
		sw::SwitchCounters counters;
		sw::RegisterPairs pairs;
		sw::EyeSwitch<RecordingPort> sky{&port, &counters, &eyes, 0.0f};
		sky.pairs = &pairs;
		sky.viewportOnly = true;
		sky.Switch(1);
		sky.Switch(0);
		sw::EyeSwitch<RecordingPort> none{&port, &counters, nullptr, 0.0f};
		none.pairs = &pairs;
		none.Switch(1);
		none.Switch(0);
		bool read = false;
		for (const std::string& step : port.steps) {
			read = read || step == "read registers" || step == "write registers";
		}
		Check(!read, "no register read or written");
	}

	Section("DifferingRegisters and ForRegisterRuns");
	{
		float a[sw::kCarriedRegisters * 4] = {};
		float b[sw::kCarriedRegisters * 4] = {};
		Check(sw::DifferingRegisters(a, b) == 0, "equal");
		b[47 * 4 + 2] = 1.0f;
		b[0] = -0.0f;
		Check(sw::DifferingRegisters(a, b) == ((std::uint64_t{1} << 47) | 1u), "the last register and a sign of zero (bits)");
		std::vector<std::string> runs;
		sw::ForRegisterRuns((std::uint64_t{3} << 46) | 0x16u, [&runs](std::uint32_t start, std::uint32_t count) {
			runs.push_back(std::to_string(start) + "+" + std::to_string(count));
		});
		Check(runs == std::vector<std::string>{"1+2", "4+1", "46+2"}, "runs in order, the top run ends at the last register");
		runs.clear();
		sw::ForRegisterRuns(0, [&runs](std::uint32_t, std::uint32_t) { runs.push_back("x"); });
		Check(runs.empty(), "no bit, no call");
	}

	Section("HasAncestor: one of the two camera-following roots above the geometry, itself included");
	{
		// 10 -> 20 -> 30 -> 0;  40 -> 50 -> 0
		const auto parentOf = [](std::uint32_t object) -> std::uint32_t {
			switch (object) {
				case 10: return 20;
				case 20: return 30;
				case 40: return 50;
				default: return 0;
			}
		};
		Check(sw::HasAncestor(10, 30, 99, parentOf), "the first root two levels up");
		Check(sw::HasAncestor(10, 99, 30, parentOf), "the second root two levels up");
		Check(sw::HasAncestor(30, 30, 99, parentOf), "the root itself");
		Check(!sw::HasAncestor(40, 30, 20, parentOf), "another branch");
		Check(!sw::HasAncestor(10, 0, 0, parentOf), "null roots match nothing");
		Check(!sw::HasAncestor(0, 30, 20, parentOf), "no object");
		std::uint32_t steps = 0;
		const auto loop = [&steps](std::uint32_t) -> std::uint32_t {
			++steps;
			return 7;
		};
		Check(!sw::HasAncestor(7, 30, 20, loop) && steps == sw::kMaxAncestors, "a cycle ends after the depth limit");
	}

	return Failures();
}
