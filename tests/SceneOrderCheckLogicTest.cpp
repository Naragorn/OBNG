// [Probe] SceneOrderCheck (src/engine/scene/SceneOrderCheckLogic.h): the mode
// from the INI, the counts and the line.

#include "Check.h"
#include "engine/scene/SceneOrderCheckLogic.h"

#include <cstring>
#include <string>

namespace oc = obng::engine::scene::order_check;

int main() {
	Section("The mode from the INI");
	Check(oc::ModeFromIni(0) == oc::Mode::kOff && oc::ModeFromIni(1) == oc::Mode::kTime && oc::ModeFromIni(2) == oc::Mode::kTwice, "0, 1 and 2");
	Check(oc::ModeFromIni(7) == oc::Mode::kOff && oc::ModeFromIni(-1) == oc::Mode::kOff, "anything else: off");

	Section("The counts");
	oc::Counts c;
	c.Add(60, 1000, 250, true);
	c.Add(40, 1000, 250, false);
	Check(c.checks == 2 && c.entries == 100 && c.firstTicks == 2000 && c.secondTicks == 500 && c.differed == 1, "summed, a differing answer counted");

	Section("The line");
	char line[400];
	int n = oc::Format(c, oc::Mode::kTwice, 1000.0, line, sizeof(line));
	const std::string twice(line);
	Check(n > 0 && static_cast<std::size_t>(n) == std::strlen(line), "the length is the line's");
	Check(twice.find("2 checks of 100 entries: the first 2.000 ms in all (1000.00 us a check, 20000.0 ns an entry)") != std::string::npos, "the first check's time");
	Check(twice.find("the same again at once 0.500 ms - 75 % of the first is fetching the tree; 1 answers differed") != std::string::npos, "the second, the share, the differing");
	oc::Format(c, oc::Mode::kTime, 1000.0, line, sizeof(line));
	Check(std::string(line).find("(timed)") != std::string::npos && std::string(line).find("again") == std::string::npos, "timed only: no second check");
	oc::Counts slow;
	slow.Add(10, 100, 300, true);
	oc::Format(slow, oc::Mode::kTwice, 1000.0, line, sizeof(line));
	Check(std::string(line).find("- 0 % of the first") != std::string::npos, "a second check slower than the first: no share claimed");
	oc::Format(oc::Counts{}, oc::Mode::kTwice, 0.0, line, sizeof(line));
	Check(std::string(line).find("0 checks of 0 entries: the first 0.000 ms") != std::string::npos, "nothing counted, no calibration: zeros, no division by zero");
	struct {
		char tiny[8];
		char guard[4];
	} box = {{}, {'a', 'b', 'c', 0}};
	oc::Format(c, oc::Mode::kTwice, 1000.0, box.tiny, sizeof(box.tiny));
	Check(box.tiny[7] == '\0' && std::strcmp(box.guard, "abc") == 0, "a short buffer is not overrun");
	Check(oc::Format(c, oc::Mode::kTwice, 1000.0, box.tiny, 0) == 0, "no room: nothing written");
	return Failures();
}
