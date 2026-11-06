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
	Check(std::string(line).find("the shader's fade") == std::string::npos, "no fade sample, no fade text");
	gr::Fade sample;
	sample.distance = 5200.0f;
	sample.fade = 0.5f;
	sample.ndcX = -0.9f;
	sample.inView = true;
	pass.fade.Add(sample);
	char longLine[1100];
	gr::Format(pass, 1ull, "", longLine, sizeof(longLine));
	Check(std::string(longLine).find("farthest 2500; the shader's fade at the blade positions of the grass draws (samples:mean per band) middle [- - - - - - - -] "
	                                 "between [- - - - - - - -] edge [- - - - - 1:0.50 - -]; qpc 1, ") != std::string::npos,
	      "with samples the table stands before the clock");
}

void TestShaderFade() {
	Section("ShaderFade: the grass vertex shaders' fade from the device's constants, as GRASS20xx.vso computes it");
	// A projection with half-width tangent 1.4 and half-height 1.1, the
	// view the identity: clip = (x / 1.4, y / 1.1, z, z) for a far plane far away.
	const float rows[16] = {1.0f / 1.4f, 0, 0, 0, 0, 1.0f / 1.1f, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0};
	const float alpha[4] = {0.0f, 1.0f, 7000.0f, 1000.0f};  // near fade 0..1, far fade 7000..8000
	const float p11 = 1.0f / 1.4f;
	const float p22 = 1.0f / 1.1f;
	const float ahead[3] = {0.0f, 0.0f, 5000.0f};
	gr::Fade f = gr::ShaderFade(alpha, rows, ahead, p11, p22);
	Check(Near(f.distance, 5000.0f, 0.5f) && Near(f.length, 7071.07f, 0.5f), "straight ahead the length is 1.41 times the distance");
	Check(Near(f.fade, 1.0f - 0.07107f, 0.001f) && Near(f.ndcX, 0.0f) && Near(f.ndcY, 0.0f), "5000 away ahead: fading already");
	const float aheadFar[3] = {0.0f, 0.0f, 5700.0f};
	Check(gr::ShaderFade(alpha, rows, aheadFar, p11, p22).fade == 0.0f, "5700 away ahead: gone");
	// The same distance at the picture's horizontal edge: x = 1.4 z.
	const float z = 5700.0f / std::sqrt(1.0f + 1.4f * 1.4f);
	const float edge[3] = {1.4f * z, 0.0f, z};
	f = gr::ShaderFade(alpha, rows, edge, p11, p22);
	Check(Near(f.distance, 5700.0f, 0.5f) && Near(f.ndcX, 1.0f, 0.001f), "the edge of the view at the same distance");
	Check(Near(f.length, z * std::sqrt(3.0f), 0.5f) && f.fade == 1.0f, "there the length is 1.007 times the distance: fully drawn");
	const float near[3] = {0.0f, 0.0f, 0.25f};
	f = gr::ShaderFade(alpha, rows, near, p11, p22);
	Check(Near(f.fade, 0.3536f, 0.001f), "the near fade: length 0.354 of the 0..1 ramp");
	const float behind[3] = {0.0f, 0.0f, -100.0f};
	f = gr::ShaderFade(alpha, rows, behind, p11, p22);
	Check(!f.inView, "behind the camera: not in the view");
	const float outside[3] = {3.0f * 1000.0f, 0.0f, 1000.0f};
	// Just inside the edge (on it the rounding decides): x = 1.35 z.
	const float zInside = 5565.0f / std::sqrt(1.0f + 1.35f * 1.35f);
	const float nearEdge[3] = {1.35f * zInside, 0.0f, zInside};
	Check(!gr::ShaderFade(alpha, rows, outside, p11, p22).inView && gr::ShaderFade(alpha, rows, nearEdge, p11, p22).inView &&
	          gr::ShaderFade(alpha, rows, ahead, p11, p22).inView,
	      "beyond the picture's edge: not in the view; near the edge and in the middle it is");
	const float zeroRange[4] = {0.0f, 0.0f, 7000.0f, 0.0f};
	const float closer[3] = {0.0f, 0.0f, 4000.0f};  // length 5657, before the far start
	f = gr::ShaderFade(zeroRange, rows, closer, p11, p22);
	Check(f.fade == 1.0f, "a zero range does not divide: before the far start the blade is drawn");
	f = gr::ShaderFade(zeroRange, rows, aheadFar, p11, p22);
	Check(f.fade == 0.0f, "and beyond it not");

	Section("FadeTable: the samples by distance band and part of the view");
	gr::FadeTable table;
	Check(table.total == 0, "empty");
	table.Add(gr::ShaderFade(alpha, rows, ahead, p11, p22));    // band 5, the middle
	table.Add(gr::ShaderFade(alpha, rows, aheadFar, p11, p22)); // band 5, the middle
	table.Add(gr::ShaderFade(alpha, rows, nearEdge, p11, p22)); // band 5, the edge
	table.Add(gr::ShaderFade(alpha, rows, behind, p11, p22));   // not counted
	Check(table.total == 3 && table.cell[0][5].count == 2 && Near(table.cell[0][5].sum, 0.92893f, 0.001f) && table.cell[2][5].count == 1 &&
	          table.cell[2][5].sum == 1.0f && table.cell[1][5].count == 0,
	      "two in the middle, one at the edge, the one behind left out");
	Check(gr::ViewPart(0.0f) == 0 && gr::ViewPart(-0.33f) == 0 && gr::ViewPart(0.34f) == 1 && gr::ViewPart(-0.66f) == 1 &&
	          gr::ViewPart(0.67f) == 2 && gr::ViewPart(1.0f) == 2,
	      "the view in thirds of its half-width");
	char text[400];
	gr::FormatFade(table, text, sizeof(text));
	Check(std::string(text) == "middle [- - - - - 2:0.46 - -] between [- - - - - - - -] edge [- - - - - 1:1.00 - -]", "the table's text");
}

}  // namespace

int main() {
	TestShaderFade();
	TestBands();
	TestHeading();
	TestWriter();
	TestFormat();
	return Failures();
}
