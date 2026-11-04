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
	Check(s.Asked() == 2 && route::PlanFor(s, true).eyes == 2 && obng::engine::render::stereo_draw::DrawsTwice(route::PlanFor(s, true)),
	      "on with two eyes and eye data: two detours, a two-eye plan that draws twice");
	Check(route::PlanFor(s, false).eyes == 1, "on with two eyes but no eye data: one eye");
	s.rangeWalk = true;
	Check(s.Asked() == 3, "with the range walk: three");

	Section("The eye switch counters fold into the stats");
	route::Stats stats;
	obng::plugin::stereo_eye_switch::SwitchCounters counters;
	counters.applied = 2;
	counters.noData = 1;
	counters.restored = 2;
	stats.AddSwitches(counters);
	Check(stats.eyeSwitches == 3 && stats.eyeSwitchesApplied == 2, "three Switch(1) calls, two of them applied");
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
	Check(stats.wholeDraws == 3 && stats.wholeOutcome[0] == 2 && stats.wholeOutcome[1] == 1 && stats.wholeOutcome[2] == 0,
	      "three whole draws: two drawn, one refused");
	Check(stats.skinnedDraws == 3 && stats.skinnedOutcome[3] == 1 && stats.skinnedOutcome[0] == 1 && stats.skinnedOutcome[2] == 1,
	      "three skinned draws: closed, no skin, record missing");
	char line[400];
	route::FormatStats(stats, 0.0, line, sizeof(line));
	Check(std::string(line) ==
	          "Route: stereo draw (eyes 2) - 3 whole draws (drawn 2, refused 1, null record 0), 3 skinned draws (closed 1, no skin 1, "
	          "refused 0, record missing 1), 3 eye switches (applied 2), 4 draws outside eye 0's viewport, eye data set 5, refused 1",
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
