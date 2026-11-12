// The stereo draw route's decisions (plugin/src/RouteStereoDrawLogic.h):
// the plan from the settings, the counters, the counting eye switch and the
// summary line.

#include "Check.h"
#include "RouteSettings.h"
#include "RouteStereoDrawLogic.h"

#include <cstring>
#include <string>

namespace route = obng::plugin::stereo_draw_route;
using obng::plugin::RouteSettings;
using obng::plugin::StereoEyesFromIni;

int main() {
	Section("[Route] StereoDrawTimer: the steps' line");
	{
		route::StepTimes t;
		char line[600];
		route::FormatStepTimes(t, 1000.0, line, sizeof(line));
		Check(std::string(line) == "Route: stereo draw steps - none timed", "no draw: said so");
		Check(!t.AddSlot(9, 5) && !t.AddSlot(20, 5), "a slot outside 10..19 is not added");
		Check(t.AddSlot(10, 500) && t.AddSlot(19, 250) && t.AddSlot(19, 250), "slots 10 and 19 are the ends");
		t.wholeCalls = 4;
		t.wholeTicks = 8000;
		t.stateCalls = 3;
		t.stateTicks = 1500;
		route::FormatStepTimes(t, 0.0, line, sizeof(line));
		Check(std::string(line) == "Route: stereo draw steps - none timed", "no calibration: said so");
		route::FormatStepTimes(t, 1000.0, line, sizeof(line));
		Check(std::string(line) == "Route: stereo draw steps (the whole draw's) - 4 draws, 8.000 ms in all; render state calls 3, 1.500 ms; slot 10 1 "
		                           "calls, 0.500 ms; slot 19 2 calls, 0.500 ms",
		      "the draws, the render state's calls and only the slots that ran");
	}
	Section("[Route] StereoDrawTimer: the device calls' line");
	{
		char line[200];
		route::FormatDeviceDraws(2000, 3000, 1000.0, line, sizeof(line));
		Check(std::string(line) == "Route: stereo draw device calls (the whole draw's) - 2000 calls, 3.000 ms in all, 1.50 us each",
		      "calls, the time in all and the time a call");
		route::FormatDeviceDraws(0, 3000, 1000.0, line, sizeof(line));
		Check(std::string(line) == "Route: stereo draw device calls - none timed", "no call: said so, no division");
		route::FormatDeviceDraws(5, 3000, 0.0, line, sizeof(line));
		Check(std::string(line) == "Route: stereo draw device calls - none timed", "no calibration: said so");
	}
	Section("[Route] StereoEyes: 1 and 2 are known, anything else reads as 1 and is flagged");
	bool known = false;
	Check(StereoEyesFromIni(1, &known) == 1 && known, "1 -> one eye, known");
	Check(StereoEyesFromIni(2, &known) == 2 && known, "2 -> two eyes, known");
	Check(StereoEyesFromIni(0, &known) == 1 && !known, "0 -> one eye, unknown");
	Check(StereoEyesFromIni(3, &known) == 1 && !known, "3 -> one eye, unknown");
	Check(StereoEyesFromIni(-1, &known) == 1 && !known, "-1 -> one eye, unknown");

	Section("[Route] StereoReapply: slot14 and view are known, anything else reads as view and is flagged");
	Check(obng::plugin::StereoReapplyFromIni("slot14", &known) == obng::plugin::StereoReapply::kSlot14 && known, "slot14");
	Check(obng::plugin::StereoReapplyFromIni("view", &known) == obng::plugin::StereoReapply::kView && known, "view");
	Check(obng::plugin::StereoReapplyFromIni("none", &known) == obng::plugin::StereoReapply::kNone && known, "none");
	Check(std::string(obng::plugin::StereoReapplyName(obng::plugin::StereoReapply::kNone)) == "none", "none's name");
	Check(obng::plugin::StereoReapplyFromIni("slot1", &known) == obng::plugin::StereoReapply::kView && !known, "a prefix: view, unknown");
	Check(obng::plugin::StereoReapplyFromIni("", &known) == obng::plugin::StereoReapply::kView && !known, "empty: view, unknown");
	Check(std::string(obng::plugin::StereoReapplyName(obng::plugin::StereoReapply::kSlot14)) == "slot14" &&
	          std::string(obng::plugin::StereoReapplyName(obng::plugin::StereoReapply::kView)) == "view",
	      "the names");

	Section("The plan and the detour count follow the settings");
	RouteSettings s;
	Check(s.Asked() == 0 && route::PlanFor(s, true).eyes == 1, "off: nothing asked, a one-eye plan");
	s.stereoDraw = true;
	s.stereoEyes = 2;
	Check(s.Asked() == 4 && route::PlanFor(s, true).eyes == 2 && obng::engine::render::stereo_draw::DrawsTwice(route::PlanFor(s, true)),
	      "on with two eyes and eye data: four detours, a two-eye plan that draws twice");
	Check(route::PlanFor(s, false).eyes == 1, "on with two eyes but no eye data: one eye");
	s.rangeWalk = true;
	Check(s.Asked() == 5, "with the range walk: five");
	s.occlusionCull = true;
	Check(s.Asked() == 8, "with the occlusion culler: its three detours as well");
	s.stereoDraw = false;
	Check(s.Asked() == 1, "the culler without the route asks for nothing (it needs the route's eye data)");
	s.stereoDraw = true;

	Section("The particle draw's device call: once for one eye; for two the draw, the switch, the draw again, the switch back");
	{
		struct Recorder {
			std::string steps;
			int results[2] = {17, 99};
			int draws = 0;
			std::int32_t Draw() {
				steps += "draw ";
				return results[draws++];
			}
			void Switch(std::uint8_t eye) { steps += eye == 1 ? "switch1 " : "switch0 "; }
		};
		Recorder one;
		Check(route::DrawWithPlan(obng::engine::render::stereo_draw::EyePlan{1}, one, one) == 17 && one.steps == "draw ", "one eye: the call as it was");
		Recorder two;
		Check(route::DrawWithPlan(obng::engine::render::stereo_draw::EyePlan{2}, two, two) == 17 && two.steps == "draw switch1 draw switch0 ",
		      "two eyes: the second draw between the switches; the first draw's result returned");
	}

	Section("The eye switch counters fold into the stats");
	route::Stats stats;
	obng::plugin::stereo_eye_switch::SwitchCounters counters;
	counters.applied = 2;
	counters.noData = 1;
	counters.restored = 2;
	counters.viewportOnly = 1;
	counters.carriedRegisters = 8;
	stats.AddSwitches(counters);
	Check(stats.eyeSwitches == 3 && stats.eyeSwitchesApplied == 2 && stats.eyeSwitchesViewportOnly == 1 && stats.carriedRegisters == 8,
	      "three Switch(1) calls, two of them applied, one of those for a camera follower; eight registers carried");
	stats.eyeDataSets = 5;
	stats.eyeDataRefused = 1;
	stats.drawsOutsideEye = 4;

	Section("The counters by function and outcome; Reset keeps the plan");
	stats.eyes = 2;
	stats.AddWhole(route::whole::Outcome::Drawn);
	stats.AddWhole(route::whole::Outcome::PipelineRefused);
	stats.AddWhole(route::whole::Outcome::Drawn);
	stats.AddSkinned(route::skinned::Outcome::kClosed);
	stats.AddSkinned(route::skinned::Outcome::kNoSkin);
	stats.AddSkinned(route::skinned::Outcome::kRecordMissing);
	stats.directShapeDraws = 30;
	stats.directStripsDraws = 500;
	stats.particleDraws = 7;
	Check(stats.wholeDraws == 3 && stats.wholeOutcome[0] == 2 && stats.wholeOutcome[1] == 1 && stats.wholeOutcome[2] == 0,
	      "three whole draws: two drawn, one refused");
	Check(stats.skinnedDraws == 3 && stats.skinnedOutcome[3] == 1 && stats.skinnedOutcome[0] == 1 && stats.skinnedOutcome[2] == 1,
	      "three skinned draws: closed, no skin, record missing");
	char line[1400];
	route::FormatStats(stats, 0.0, line, sizeof(line));
	Check(std::string(line) ==
	          "Route: stereo draw (eyes 2) - 3 whole draws (drawn 2, refused 1, null record 0), 3 skinned draws (closed 1, no skin 1, "
	          "refused 0, record missing 1), 30 direct shape draws, 500 direct strips draws, 7 particle draws, 3 eye switches (applied 2, 1 of them without a view shift, 8 registers carried), 4 draws outside eye 0's viewport, eye data set 5, refused 1",
	      "the line");
	stats.outside.Add(obng::plugin::stereo_eye_switch::Viewport{0, 0, 1024, 1024, 0.0f, 1.0f});
	stats.outside.Add(obng::plugin::stereo_eye_switch::Viewport{0, 0, 1024, 1024, 0.0f, 1.0f});
	stats.outside.Add(obng::plugin::stereo_eye_switch::Viewport{0, 0, 5136, 2568, 0.0f, 1.0f});
	route::FormatStats(stats, 0.0, line, sizeof(line));
	Check(std::strstr(line, "; outside viewports: 0,0 1024x1024 x2 0,0 5136x2568 x1") != nullptr, "the outside viewports with their counts");
	for (std::uint32_t i = 0; i < route::OutsideViewports::kMaxRows + 2; ++i) {
		stats.outside.Add(obng::plugin::stereo_eye_switch::Viewport{i, 0, 10, 10, 0.0f, 1.0f});
	}
	Check(stats.outside.size == route::OutsideViewports::kMaxRows && stats.outside.overflow == 4, "rects beyond the table are counted");
	stats.shaderClasses.Add(0x00A8977C);
	stats.shaderClasses.Add(0x00A9459C);
	stats.shaderClasses.Add(0x00A8977C);
	stats.reappliesType1 = 1;
	route::FormatStats(stats, 0.0, line, sizeof(line));
	Check(std::strstr(line, "; shader classes (vtable x switches): 00A8977C x2 00A9459C x1; 1 re-applies through the type-1 shader's maps") != nullptr,
	      "the shader classes with their counts");
	stats.AddTicks(6000, 2000);
	route::FormatStats(stats, 1000.0, line, sizeof(line));  // 1000 ticks a millisecond: a tick is a microsecond
	Check(std::strstr(line, "; switch 3000.00 us, re-apply 1000.00 us per applied switch") != nullptr, "the timing per applied switch (two applied)");
	route::FormatStats(stats, 0.0, line, sizeof(line));
	Check(std::strstr(line, "switch ") == nullptr, "no clock: no timing");
	stats.Reset();
	Check(stats.wholeDraws == 0 && stats.skinnedDraws == 0 && stats.eyeSwitches == 0 && stats.eyes == 2, "reset clears the counts, keeps the eyes");
	char tiny[8];
	route::FormatStats(stats, 1000.0, tiny, sizeof(tiny));
	Check(std::strlen(tiny) < sizeof(tiny), "a small buffer is never overrun");
	return Failures();
}
