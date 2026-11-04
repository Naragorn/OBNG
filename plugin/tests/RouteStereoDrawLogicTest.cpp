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

	Section("The plan and the detour count follow the settings");
	RouteSettings s;
	Check(s.Asked() == 0 && route::PlanFor(s).eyes == 1, "off: nothing asked, a one-eye plan");
	s.stereoDraw = true;
	s.stereoEyes = 2;
	Check(s.Asked() == 2 && route::PlanFor(s).eyes == 2 && obng::engine::render::stereo_draw::DrawsTwice(route::PlanFor(s)),
	      "on with two eyes: two detours, a two-eye plan that draws twice");
	s.rangeWalk = true;
	Check(s.Asked() == 3, "with the range walk: three");

	Section("The counting eye switch counts Switch(1) only");
	route::Stats stats;
	route::CountingEyeSwitch sw{&stats};
	sw.Switch(1);
	sw.Switch(0);
	sw.Switch(1);
	Check(stats.eyeSwitches == 2, "two second-eye frames");

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
	route::FormatStats(stats, line, sizeof(line));
	Check(std::string(line) ==
	          "Route: stereo draw (eyes 2) - 3 whole draws (drawn 2, refused 1, null record 0), 3 skinned draws (closed 1, no skin 1, "
	          "refused 0, record missing 1), 2 eye switches",
	      "the line");
	stats.Reset();
	Check(stats.wholeDraws == 0 && stats.skinnedDraws == 0 && stats.eyeSwitches == 0 && stats.eyes == 2, "reset clears the counts, keeps the eyes");
	char tiny[8];
	route::FormatStats(stats, tiny, sizeof(tiny));
	Check(std::strlen(tiny) < sizeof(tiny), "a small buffer is never overrun");
	return Failures();
}
