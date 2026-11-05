// The unrouted-draw probe's decisions (plugin/src/UnroutedDrawLogic.h): where
// a device draw falls, the table of the sites that issue the draws the stereo
// route does not double, and the summary lines.

#include "Check.h"
#include "UnroutedDrawLogic.h"

#include <string>

namespace un = obng::plugin::unrouted;
using un::Method;
using un::Rect;
using un::Stats;
using un::Where;

namespace {

const Rect kEyeZero{0, 0, 2568, 2568};
const Rect kEyeOne{2568, 0, 2568, 2568};
const Rect kSmall{0, 0, 512, 512};

void TestClassify() {
	Section("Where a device draw falls");
	Check(un::Classify(true, true, kEyeZero, kEyeZero) == Where::kRouted, "inside a routed draw: routed, whatever the viewport");
	Check(un::Classify(true, false, kSmall, kEyeZero) == Where::kRouted, "inside a routed draw without eye data: routed");
	Check(un::Classify(false, true, kEyeZero, kEyeZero) == Where::kEyeZero, "outside the route in eye 0's viewport: the second eye misses it");
	Check(un::Classify(false, true, kEyeOne, kEyeZero) == Where::kOtherViewport, "another x");
	Check(un::Classify(false, true, Rect{0, 8, 2568, 2568}, kEyeZero) == Where::kOtherViewport, "another y");
	Check(un::Classify(false, true, Rect{0, 0, 5136, 2568}, kEyeZero) == Where::kOtherViewport, "another width (both eyes at once)");
	Check(un::Classify(false, true, Rect{0, 0, 2568, 1444}, kEyeZero) == Where::kOtherViewport, "another height");
	Check(un::Classify(false, false, kEyeZero, kEyeZero) == Where::kNoEyes, "no eye data: not a single-pass frame");
	Check(std::string(un::WhereName(Where::kRouted)) == "routed" && std::string(un::WhereName(Where::kEyeZero)) == "in eye 0's viewport" &&
	          std::string(un::WhereName(Where::kOtherViewport)) == "in another viewport" &&
	          std::string(un::WhereName(Where::kNoEyes)) == "without eye data" && std::string(un::WhereName(Where::kCount)) == "?",
	      "the names");
	Check(std::string(un::MethodName(Method::kDrawPrimitive)) == "DrawPrimitive" &&
	          std::string(un::MethodName(Method::kDrawIndexedPrimitive)) == "DrawIndexedPrimitive" &&
	          std::string(un::MethodName(Method::kDrawPrimitiveUP)) == "DrawPrimitiveUP" &&
	          std::string(un::MethodName(Method::kDrawIndexedPrimitiveUP)) == "DrawIndexedPrimitiveUP" &&
	          std::string(un::MethodName(Method::kCount)) == "?",
	      "the methods' names");
}

void TestTable() {
	Section("Routed draws are counted; every other draw also goes to its site's row");
	Stats stats;
	stats.Add(Where::kRouted, Method::kDrawIndexedPrimitive, 0x10001000u, kEyeZero, 100);
	stats.Add(Where::kRouted, Method::kDrawIndexedPrimitive, 0x10001000u, kEyeOne, 100);
	Check(stats.calls[static_cast<int>(Where::kRouted)] == 2 && stats.sites.used == 0, "two routed, no row");
	stats.Add(Where::kEyeZero, Method::kDrawPrimitive, 0x00766A00u, kEyeZero, 2);
	stats.Add(Where::kEyeZero, Method::kDrawPrimitive, 0x00766A00u, kEyeZero, 4);
	stats.Add(Where::kEyeZero, Method::kDrawIndexedPrimitive, 0x00766A00u, kEyeZero, 1);
	stats.Add(Where::kOtherViewport, Method::kDrawPrimitive, 0x00766A00u, kSmall, 2);
	stats.Add(Where::kNoEyes, Method::kDrawPrimitiveUP, 0x007B5000u, kSmall, 2);
	Check(stats.calls[static_cast<int>(Where::kEyeZero)] == 3 && stats.calls[static_cast<int>(Where::kOtherViewport)] == 1 &&
	          stats.calls[static_cast<int>(Where::kNoEyes)] == 1,
	      "the totals by where");
	Check(stats.sites.used == 4, "four rows: the key is the site, the method and where");
	const un::SiteRow* row = stats.sites.Find(0x00766A00u, Method::kDrawPrimitive, Where::kEyeZero);
	Check(row != nullptr && row->calls == 2 && row->primitives == 6 && row->viewport.width == 2568 && !row->moreViewports, "calls and primitives add up");
	Check(stats.sites.Find(0x00766A00u, Method::kDrawPrimitiveUP, Where::kEyeZero) == nullptr, "no row for what was not seen");

	Section("A site that draws into more than one viewport says so and keeps the first");
	stats.Add(Where::kOtherViewport, Method::kDrawPrimitive, 0x00766A00u, Rect{0, 0, 256, 256}, 2);
	const un::SiteRow* other = stats.sites.Find(0x00766A00u, Method::kDrawPrimitive, Where::kOtherViewport);
	Check(other != nullptr && other->calls == 2 && other->viewport.width == 512 && other->moreViewports, "the first viewport kept, the flag set");

	Section("The rows by their calls, the most first; ties in the table's order");
	Check(stats.sites.Ranked(0) == stats.sites.Find(0x00766A00u, Method::kDrawPrimitive, Where::kEyeZero), "two calls, the first such row");
	Check(stats.sites.Ranked(1) == other, "two calls, the later row");
	Check(stats.sites.Ranked(2)->calls == 1 && stats.sites.Ranked(3)->calls == 1 && stats.sites.Ranked(2) != stats.sites.Ranked(3), "then the single ones");
	Check(stats.sites.Ranked(4) == nullptr, "nothing past the rows");

	Section("More sites than rows: the rest is counted");
	Stats full;
	for (std::uint32_t i = 0; i < un::kSiteRows + 3; ++i) {
		full.Add(Where::kEyeZero, Method::kDrawPrimitive, 0x00700000u + i, kEyeZero, 1);
	}
	Check(full.sites.used == un::kSiteRows && full.sites.beyond == 3 && full.calls[static_cast<int>(Where::kEyeZero)] == un::kSiteRows + 3,
	      "three calls beyond the table, all in the total");

	Section("A where or a method out of range is dropped; Reset clears");
	full.Add(Where::kCount, Method::kDrawPrimitive, 1, kEyeZero, 1);
	full.Add(Where::kEyeZero, Method::kCount, 1, kEyeZero, 1);
	Check(full.calls[static_cast<int>(Where::kEyeZero)] == un::kSiteRows + 3, "nothing added");
	full.Reset();
	Check(full.sites.used == 0 && full.sites.beyond == 0 && full.calls[static_cast<int>(Where::kEyeZero)] == 0, "zero");
}

void TestFormat() {
	Section("The summary: a head line and one line per site");
	Stats stats;
	stats.Add(Where::kRouted, Method::kDrawIndexedPrimitive, 0x10001000u, kEyeZero, 100);
	stats.Add(Where::kEyeZero, Method::kDrawPrimitive, 0x00766A00u, kEyeZero, 2);
	stats.Add(Where::kEyeZero, Method::kDrawPrimitive, 0x00766A00u, kEyeZero, 4);
	stats.Add(Where::kOtherViewport, Method::kDrawPrimitiveUP, 0x007B5000u, kSmall, 2);
	stats.Add(Where::kOtherViewport, Method::kDrawPrimitiveUP, 0x007B5000u, Rect{0, 0, 256, 256}, 2);
	stats.Add(Where::kOtherViewport, Method::kDrawPrimitiveUP, 0x007B5000u, kSmall, 2);
	char line[400];
	Check(un::FormatHead(stats, 987654321ull, line, sizeof(line)) > 0, "a head line");
	Check(std::string(line) ==
	          "Probe: device draws - 1 routed, 2 in eye 0's viewport outside the route, 3 in another viewport, 0 without eye data, 2 sites, 0 "
	          "calls beyond the table; qpc 987654321",
	      "the head line");
	Check(un::FormatSite(stats, 0, line, sizeof(line)) > 0, "the first site");
	Check(std::string(line) ==
	          "Probe: device draw site 0x007B5000 DrawPrimitiveUP in another viewport - 3 calls, 6 primitives, viewport 0,0 512x512 and others",
	      "the site with the most calls, its first viewport, the others flagged");
	Check(un::FormatSite(stats, 1, line, sizeof(line)) > 0 &&
	          std::string(line) == "Probe: device draw site 0x00766A00 DrawPrimitive in eye 0's viewport - 2 calls, 6 primitives, viewport 0,0 2568x2568",
	      "the second");
	Check(un::FormatSite(stats, 2, line, sizeof(line)) == 0, "no line past the rows");
}

}  // namespace

int main() {
	TestClassify();
	TestTable();
	TestFormat();
	return Failures();
}
