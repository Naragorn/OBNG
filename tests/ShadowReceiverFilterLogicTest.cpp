// Every flow of the actor shadows' receiver filter (src/engine/render/ShadowReceiverFilterLogic.h).

#include "Check.h"
#include "engine/render/ShadowReceiverFilterLogic.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace obng::engine::render::shadow_receiver_filter;

namespace {

Receiver Near(float radius, float gap = 0.0f, bool skinned = false) {
	Receiver r;
	r.castsShadow = true;
	r.hasCaster = true;
	r.radius = radius;
	r.gap = gap;
	r.skinned = skinned;
	return r;
}

}  // namespace

int main() {
	Section("The settings from the INI");
	{
		Check(!SettingsFrom(0.0f, 0.0f, 0).On(), "both 0: off");
		Check(SettingsFrom(16.0f, 0.0f, 0).On() && SettingsFrom(16.0f, 0.0f, 0).minRadius == 16.0f, "a radius alone: on");
		Check(SettingsFrom(0.0f, 256.0f, 0).On() && SettingsFrom(0.0f, 256.0f, 0).maxGap == 256.0f, "a gap alone: on");
		Check(!SettingsFrom(-3.0f, -1.0f, 0).On(), "negative: off");
		Check(!SettingsFrom(std::nanf(""), std::nanf(""), 0).On(), "NaN: off");
		Check(SettingsFrom(5000.0f, 0.0f, 0).minRadius == 1000.0f, "the radius clamped to 1000 units");
		Check(SettingsFrom(0.0f, 1.0e6f, 0).maxGap == 100000.0f, "the gap clamped to 100 000 units");
		Check(SettingsFrom(16.0f, 0.0f, 1).alternate && !SettingsFrom(16.0f, 0.0f, 0).alternate, "the alternation");
	}

	Section("The buckets");
	{
		Check(RadiusBucket(0.0f) == 0 && RadiusBucket(3.9f) == 0, "radius below 4: bucket 0");
		Check(RadiusBucket(4.0f) == 1 && RadiusBucket(16.0f) == 3 && RadiusBucket(127.9f) == 5, "4, 16, 127.9");
		Check(RadiusBucket(128.0f) == 6 && RadiusBucket(1.0e6f) == 6 && RadiusBucket(std::nanf("")) == kBuckets - 1, "128 and more, NaN: the last");
		Check(GapBucket(-50.0f) == 0 && GapBucket(0.0f) == 0, "the caster inside the receiver's bound: gap bucket 0");
		Check(GapBucket(0.5f) == 1 && GapBucket(63.9f) == 1, "up to 64: 1");
		Check(GapBucket(64.0f) == 2 && GapBucket(1023.0f) == 5 && GapBucket(1024.0f) == 6 && GapBucket(std::nanf("")) == kBuckets - 1, "64, 1023, 1024, NaN");
		Check(std::strcmp(kRadiusName[0], "<4") == 0 && std::strcmp(kGapName[0], "<=0") == 0 && std::strcmp(kGapName[kBuckets - 1], ">=1024") == 0, "the names");
	}

	Section("The gap: from the caster's bound centre to the receiver's sphere");
	{
		const float caster[3] = {0.0f, 0.0f, 100.0f};
		const float centre[3] = {300.0f, 400.0f, 100.0f};
		Check(GapOf(caster, centre, 100.0f) == 400.0f, "500 apart, radius 100: 400");
		Check(GapOf(caster, centre, 600.0f) < 0.0f, "the caster inside the receiver's sphere: negative");
	}

	Section("The decision");
	{
		const Settings radius = SettingsFrom(16.0f, 0.0f, 0);
		Check(Decide(radius, Near(8.0f), true) == Action::kSkip, "radius 8 under 16: left out");
		Check(Decide(radius, Near(16.0f), true) == Action::kAdd, "radius 16: added");
		Check(Decide(radius, Near(8.0f, 0.0f, true), true) == Action::kSkip, "skinned as small: left out as well");
		Check(Decide(radius, Near(std::nanf("")), true) == Action::kAdd, "a NaN radius: added");
		const Settings gap = SettingsFrom(0.0f, 256.0f, 0);
		Check(Decide(gap, Near(500.0f, 300.0f), true) == Action::kSkip, "300 units beyond the caster with a gap of 256: left out");
		Check(Decide(gap, Near(500.0f, 256.0f), true) == Action::kAdd, "256: added");
		Check(Decide(gap, Near(500.0f, -10.0f), true) == Action::kAdd, "the caster inside it (the ground under it): added");
		Check(Decide(gap, Near(500.0f, std::nanf("")), true) == Action::kAdd, "a NaN gap: added");
		Check(Decide(gap, Near(2.0f, 10.0f), true) == Action::kAdd, "small but near, the radius rule off: added");
		const Settings both = SettingsFrom(16.0f, 256.0f, 0);
		Check(Decide(both, Near(8.0f, 10.0f), true) == Action::kSkip && Decide(both, Near(500.0f, 300.0f), true) == Action::kSkip &&
		          Decide(both, Near(500.0f, 10.0f), true) == Action::kAdd,
		      "both rules: either leaves out");
		Receiver lamp = Near(8.0f, 300.0f);
		lamp.castsShadow = false;
		Check(Decide(both, lamp, true) == Action::kOtherLight, "a light that casts no shadow: untouched");
		Receiver noCaster = Near(8.0f, 300.0f);
		noCaster.hasCaster = false;
		Check(Decide(both, noCaster, true) == Action::kOtherLight, "a shadow light without a caster: untouched");
		Check(Decide(both, Near(8.0f, 300.0f), false) == Action::kAdd, "off in this window: added");
		Check(Decide(SettingsFrom(0.0f, 0.0f, 0), Near(1.0f, 5000.0f), true) == Action::kAdd, "off: added");
	}

	Section("The window: on in even windows with the alternation");
	{
		Check(OnInWindow(SettingsFrom(16.0f, 0.0f, 0), 1), "no alternation: every window");
		Check(OnInWindow(SettingsFrom(16.0f, 0.0f, 1), 0) && !OnInWindow(SettingsFrom(16.0f, 0.0f, 1), 1), "alternating");
		Check(!OnInWindow(SettingsFrom(0.0f, 0.0f, 1), 0), "off: never");
	}

	Section("The counts and the summary line");
	{
		Counts c;
		const Settings s = SettingsFrom(16.0f, 256.0f, 0);
		c.Note(Near(2.0f, 10.0f), Decide(s, Near(2.0f, 10.0f), true));
		c.Note(Near(8.0f, 10.0f, true), Decide(s, Near(8.0f, 10.0f, true), true));
		c.Note(Near(300.0f, -5.0f), Decide(s, Near(300.0f, -5.0f), true));
		c.Note(Near(300.0f, 2000.0f), Decide(s, Near(300.0f, 2000.0f), true));
		Receiver lamp = Near(5.0f);
		lamp.castsShadow = false;
		c.Note(lamp, Decide(s, lamp, true));
		Check(c.calls == 5 && c.otherLights == 1 && c.skipped == 3, "five calls, one for another light, three left out");
		Check(c.byRadius[0][0] == 1 && c.byRadius[1][2] == 1 && c.byRadius[0][6] == 2, "by kind and radius");
		Check(c.byGap[1] == 2 && c.byGap[0] == 1 && c.byGap[6] == 1, "by gap");
		char line[1200];
		FormatSummary(c, s, true, 10, line, sizeof(line));
		std::printf("    %s\n", line);
		Check(std::strstr(line, "radius under 16.0") != nullptr && std::strstr(line, "beyond 256.0") != nullptr, "the settings");
		Check(std::strstr(line, "left out 3 (0.30 a step)") != nullptr, "left out, per step over 10 steps");
		Check(std::strstr(line, "by gap:") != nullptr, "the gap census");
	}

	return Failures();
}
