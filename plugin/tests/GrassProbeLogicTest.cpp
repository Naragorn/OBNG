// The grass probe's decisions (plugin/src/GrassProbeLogic.h): the distance
// bands, the camera's heading out of the view matrix, when a pass is written,
// and the line.

#include "Check.h"
#include "GrassProbeLogic.h"

#include <cmath>
#include <cstring>
#include <string>

namespace gr = obng::plugin::grass;

namespace {

bool Near(float a, float b, float tolerance = 0.01f) {
	return std::fabs(a - b) <= tolerance;
}

void TestBands() {
	Section("The distance bands: 1000 units each up to 7000, then one for everything beyond");
	Check(gr::Band(0.0f) == 0 && gr::Band(999.9f) == 0 && gr::Band(1000.0f) == 1 && gr::Band(6999.0f) == 6 && gr::Band(7000.0f) == 7 &&
	          gr::Band(50000.0f) == 7,
	      "the edges");
	Check(gr::Band(-5.0f) == 0 && gr::Band(std::nanf("")) == 0, "a distance that is none goes to the first band");

	Section("Counts: a patch adds to its band, the total and the farthest");
	gr::Counts counts;
	counts.Add(500.0f);
	counts.Add(2500.0f);
	counts.Add(2600.0f);
	counts.Add(9000.0f);
	Check(counts.total == 4 && counts.band[0] == 1 && counts.band[2] == 2 && counts.band[7] == 1 && counts.farthest == 9000.0f, "counted");
	gr::Counts same = counts;
	Check(gr::SameBands(counts, same), "equal to its copy");
	same.Add(100.0f);
	Check(!gr::SameBands(counts, same), "one more patch differs");
	gr::Counts moved;
	moved.Add(500.0f);
	moved.Add(2500.0f);
	moved.Add(3600.0f);
	moved.Add(9000.0f);
	Check(!gr::SameBands(counts, moved), "the same total in other bands differs");
}

void TestHeading() {
	Section("The camera's heading and pitch out of the view matrix (the direction is its third column)");
	float view[16] = {};
	// dir = (0, 1, 0): north, level.
	view[2] = 0.0f;
	view[6] = 1.0f;
	view[10] = 0.0f;
	Check(Near(gr::YawDegrees(view), 0.0f) && Near(gr::PitchDegrees(view), 0.0f), "along +y: heading 0, level");
	view[2] = 1.0f;
	view[6] = 0.0f;
	Check(Near(gr::YawDegrees(view), 90.0f), "along +x: heading 90");
	view[2] = -1.0f;
	Check(Near(gr::YawDegrees(view), -90.0f), "along -x: heading -90");
	view[2] = 0.0f;
	view[6] = -1.0f;
	Check(Near(std::fabs(gr::YawDegrees(view)), 180.0f), "along -y: heading 180");
	view[6] = 0.70710678f;
	view[10] = 0.70710678f;
	Check(Near(gr::PitchDegrees(view), 45.0f), "up 45 degrees");
	view[6] = 0.0f;
	view[10] = 2.0f;
	Check(Near(gr::PitchDegrees(view), 90.0f), "a component past 1 is clamped");

	Section("The turn between two headings, the short way round");
	Check(Near(gr::TurnDegrees(10.0f, 30.0f), 20.0f) && Near(gr::TurnDegrees(30.0f, 10.0f), 20.0f), "plain");
	Check(Near(gr::TurnDegrees(170.0f, -170.0f), 20.0f) && Near(gr::TurnDegrees(-179.0f, 179.0f), 2.0f), "across 180");
}

gr::Pass PassWith(float yaw, std::initializer_list<float> existing, std::initializer_list<float> drawn) {
	gr::Pass pass;
	pass.yaw = yaw;
	for (float d : existing) {
		pass.existing.Add(d);
	}
	for (float d : drawn) {
		pass.drawn.Add(d);
	}
	return pass;
}

void TestWriter() {
	Section("When a pass is written: on a change, not faster than the least interval, and at the latest after the longest");
	gr::Writer writer;
	const gr::Pass first = PassWith(0.0f, {500.0f, 2500.0f}, {500.0f});
	Check(writer.Due(first, 1000), "the first pass");
	writer.Wrote(first, 1000);
	Check(!writer.Due(first, 1050), "the same pass right after: nothing to say");
	Check(!writer.Due(first, 1000 + gr::kLongestMs - 1), "still nothing before the longest interval");
	Check(writer.Due(first, 1000 + gr::kLongestMs), "the longest interval: a line although nothing changed");
	const gr::Pass fewerDrawn = PassWith(0.0f, {500.0f, 2500.0f}, {});
	Check(!writer.Due(fewerDrawn, 1000 + gr::kLeastMs - 1), "a change inside the least interval waits");
	Check(writer.Due(fewerDrawn, 1000 + gr::kLeastMs), "a change in what is drawn");
	const gr::Pass fewerExisting = PassWith(0.0f, {500.0f}, {500.0f});
	Check(writer.Due(fewerExisting, 1000 + gr::kLeastMs), "a change in what stands under the node");
	const gr::Pass turned = PassWith(gr::kTurnDegrees, {500.0f, 2500.0f}, {500.0f});
	Check(writer.Due(turned, 1000 + gr::kLeastMs), "a turn of the head");
	const gr::Pass turnedLess = PassWith(gr::kTurnDegrees - 0.5f, {500.0f, 2500.0f}, {500.0f});
	Check(!writer.Due(turnedLess, 1000 + gr::kLeastMs), "a smaller turn is none");
	gr::Pass culled = first;
	culled.hidden.Add(2500.0f);
	Check(writer.Due(culled, 1000 + gr::kLeastMs), "a change in the patches with the hidden bit");
}

void TestFormat() {
	Section("The line");
	gr::Pass pass = PassWith(-33.25f, {500.0f, 2500.0f, 2600.0f, 6500.0f}, {500.0f, 2500.0f});
	pass.pitch = 4.5f;
	pass.camera[0] = 100.0f;
	pass.camera[1] = -200.0f;
	pass.camera[2] = 300.0f;
	pass.groups = 3;
	pass.hidden.Add(6500.0f);
	pass.perEyeData = true;
	char line[600];
	const int written = gr::Format(pass, 123456789ull, "21:15:30.123", line, sizeof(line));
	Check(written > 0 && static_cast<std::size_t>(written) < sizeof(line), "fits");
	Check(std::string(line) ==
	          "Probe: grass - heading -33.2 pitch 4.5, camera 100 -200 300; under the Grass node 3 children, 4 patches [1 0 2 0 0 0 1 0] farthest "
	          "6500, with the hidden bit 1 [0 0 0 0 0 0 1 0]; drawn in the pass 2 [1 0 1 0 0 0 0 0] farthest 2500; qpc 123456789, 21:15:30.123",
	      "the line of a single-pass frame");
	pass.perEyeData = false;
	gr::Format(pass, 1ull, "", line, sizeof(line));
	Check(std::string(line).find("drawn in the last 100 ms (no eye data: not a single-pass frame) 2 [") != std::string::npos,
	      "without eye data the draws are a time window's");
}

}  // namespace

int main() {
	TestBands();
	TestHeading();
	TestWriter();
	TestFormat();
	return Failures();
}
