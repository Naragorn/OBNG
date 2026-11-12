// Every flow of keeping a geometry's render passes through a relight that changed nothing
// (src/engine/render/PassListKeepLogic.h).

#include "Check.h"
#include "engine/render/PassListKeepLogic.h"

#include <cstdio>
#include <cstring>
#include <memory>

using namespace obng::engine::render::pass_list_keep;

namespace {

LightSet Lights(std::initializer_list<std::uint32_t> lights) {
	LightSet s;
	for (std::uint32_t l : lights) {
		s.Add(l);
	}
	return s;
}

Before Candidate(std::uint32_t key, const LightSet& lights, std::uint32_t nibble = 2) {
	Before b;
	b.ourClass = true;
	b.key = key;
	b.nibble = nibble;
	b.lights = lights;
	return b;
}

PassList Passes(std::initializer_list<std::uint16_t> ids) {
	PassList p;
	for (std::uint16_t id : ids) {
		Pass pass;
		pass.id = id;
		pass.lightCount = 1;
		pass.lights[0] = 0x9000u + id;
		p.Add(pass);
	}
	return p;
}

}  // namespace

int main() {
	Section("The mode from the INI");
	Check(ModeFromIni(0) == Mode::kOff && ModeFromIni(1) == Mode::kVerify && ModeFromIni(2) == Mode::kKeep, "0, 1, 2");
	Check(ModeFromIni(-1) == Mode::kOff && ModeFromIni(7) == Mode::kOff, "anything else: off");

	Section("A key is a real one when it is neither 0, -1 nor the sentinel");
	Check(IsRealKey(0x0102) && IsRealKey(0x0500), "a render mode and flags");
	Check(!IsRealKey(0) && !IsRealKey(0xFFFFFFFFu) && !IsRealKey(kSentinel), "cleared, node changed, ours");
	Check(kSentinel >= 0x10000u, "the sentinel is above every key (the render mode is a byte shifted by 8, the flags a byte)");

	Section("The light sets");
	{
		Check(Same(Lights({1, 2, 3}), Lights({1, 2, 3})), "the same lights in the same order");
		Check(!Same(Lights({1, 2, 3}), Lights({1, 3, 2})), "another order: the passes take the lights in order");
		Check(!Same(Lights({1, 2}), Lights({1, 2, 3})), "one more");
		Check(Same(Lights({}), Lights({})), "none and none");
		LightSet big;
		for (std::uint32_t i = 0; i < LightSet::kMax + 1; ++i) {
			big.Add(i + 1);
		}
		Check(big.overflow && big.count == LightSet::kMax, "more than kMax: overflow, the first kMax kept");
		Check(!Same(big, big), "an overflowing set is never the same (what is past kMax is unknown)");
	}

	Section("The decision after the relight");
	{
		const LightSet l = Lights({0x100, 0x200});
		Check(Decide(Candidate(0x0102, l), 0, 2, l) == Why::kKept, "a real key cleared, the node and the lights the same: kept");
		Before other = Candidate(0x0102, l);
		other.ourClass = false;
		Check(Decide(other, 0, 2, l) == Why::kNotOurClass, "another property class (its own pass getter): left");
		Check(Decide(Candidate(0, l), 0, 2, l) == Why::kKeyUnset, "cleared before: rebuilt anyway");
		Check(Decide(Candidate(0xFFFFFFFFu, l), 0, 2, l) == Why::kKeyUnset, "the node changed before: rebuilt anyway");
		Check(Decide(Candidate(0x0102, l), 0x0102, 2, l) == Why::kKeyNotCleared, "the relight did not clear it: nothing to do");
		Check(Decide(Candidate(0x0102, l), 0xFFFFFFFFu, 2, l) == Why::kKeyNotCleared, "the relight set -1 (a node change): left");
		Check(Decide(Candidate(0x0102, l, 2), 0, 3, l) == Why::kNodeChanged, "another shadow scene node: left");
		Check(Decide(Candidate(0x0102, l), 0, 2, Lights({0x100})) == Why::kLightsChanged, "a light fewer: left");
		Check(Decide(Candidate(0x0102, l), 0, 2, Lights({0x200, 0x100})) == Why::kLightsChanged, "re-sorted: left");
		LightSet big;
		for (std::uint32_t i = 0; i < LightSet::kMax + 1; ++i) {
			big.Add(i + 1);
		}
		Check(Decide(Candidate(0x0102, big), 0, 2, big) == Why::kTooManyLights, "too many lights to compare: left");
		Check(Decide(Candidate(kSentinel, l), 0, 2, l) == Why::kKept, "marked by the check before and cleared again by this relight: marked again");
		Check(Decide(Candidate(kSentinel, l), kSentinel, 2, l) == Why::kKeyNotCleared, "marked, not cleared now: left marked");
	}

	Section("What is written into the key");
	{
		const LightSet l = Lights({0x100});
		Check(KeyToWrite(Mode::kKeep, Candidate(0x0102, l)) == 0x0102u, "keep: the key from before");
		Check(KeyToWrite(Mode::kVerify, Candidate(0x0102, l)) == kSentinel, "verify: the sentinel - the getter rebuilds and the check compares");
		Check(KeyToWrite(Mode::kVerify, Candidate(kSentinel, l)) == kSentinel, "verify, marked before: the sentinel again");
	}

	Section("The pass lists compared");
	{
		std::uint32_t at = 99;
		Check(Compare(Passes({0x84, 0x177}), Passes({0x84, 0x177}), &at) == Diff::kSame, "the same passes: same");
		Check(Compare(Passes({0x84}), Passes({0x84, 0x177}), &at) == Diff::kCount && at == 1, "a pass more: count, at the first missing");
		Check(Compare(Passes({0x84, 0x177}), Passes({0x84, 0x178}), &at) == Diff::kId && at == 1, "another pass id: id, at 1");
		PassList a = Passes({0x84});
		PassList b = Passes({0x84});
		b.pass[0].lights[0] = 0x1234;
		Check(Compare(a, b, &at) == Diff::kLights && at == 0, "another light: lights");
		b = Passes({0x84});
		b.pass[0].lightCount = 2;
		Check(Compare(a, b, &at) == Diff::kLights, "another light count: lights");
		b = Passes({0x84});
		b.pass[0].flags6 = 1;
		Check(Compare(a, b, &at) == Diff::kFlags, "the byte at +6 differs: flags");
		b = Passes({0x84});
		b.pass[0].flags7 = 1;
		Check(Compare(a, b, &at) == Diff::kFlags, "the byte at +7 differs: flags");
		PassList big;
		for (std::uint32_t i = 0; i < PassList::kMax + 1; ++i) {
			Pass p;
			p.id = static_cast<std::uint16_t>(i);
			big.Add(p);
		}
		Check(big.overflow && Compare(big, big, &at) == Diff::kOverflow, "more passes than kept: not comparable");
		Pass many;
		many.lightCount = Pass::kMaxLights + 1;
		Check(!many.Comparable(), "a pass with more lights than kept: not comparable");
	}

	Section("The pending checks: by property, the last one wins, taken once");
	{
		auto pending = std::make_unique<Pending>();
		pending->Put(0x5000, 0x0102, Passes({0x84}));
		const Pending::Entry* e = pending->Find(0x5000);
		Check(e != nullptr && e->key == 0x0102 && e->passes.count == 1, "put and found");
		pending->Put(0x5000, 0x0103, Passes({0x84, 0x177}));
		e = pending->Find(0x5000);
		Check(e != nullptr && e->key == 0x0103 && e->passes.count == 2, "put again: replaced");
		pending->Drop(0x5000);
		Check(pending->Find(0x5000) == nullptr, "dropped");
		Check(pending->Find(0x6000) == nullptr, "an unknown property: none");
		for (std::uint32_t i = 0; i < Pending::kSlots + 10; ++i) {
			pending->Put(0x10000 + i * 0x40, 0x0102, Passes({0x84}));
		}
		Check(pending->Find(0x10000 + (Pending::kSlots + 9) * 0x40) != nullptr, "full: the newest still put (an older one taken over)");
	}

	Section("The counts and the summary line");
	{
		Counts c;
		c.relights = 100;
		c.why[static_cast<int>(Why::kKept)] = 60;
		c.why[static_cast<int>(Why::kLightsChanged)] = 10;
		c.getterCalls = 500;
		c.builds = 120;
		c.buildTicks = 120 * 3000;
		c.checks[static_cast<int>(Diff::kSame)] = 55;
		c.checks[static_cast<int>(Diff::kId)] = 1;
		c.otherwiseCleared = 4;
		c.frames[0] = 10;
		c.frames[1] = 10;
		c.getterTicks[0] = 20000;
		c.getterTicks[1] = 10000;
		char line[1024];
		FormatSummary(c, Mode::kVerify, false, 1000000.0, line, sizeof(line));  // 1000 ticks a microsecond
		std::printf("    %s\n", line);
		Check(std::strstr(line, "verify") != nullptr && std::strstr(line, "100 relights") != nullptr, "the mode and the relights");
		Check(std::strstr(line, "kept 60") != nullptr && std::strstr(line, "lights changed 10") != nullptr, "the reasons");
		Check(std::strstr(line, "same 55") != nullptr && std::strstr(line, "pass id 1") != nullptr, "the checks");
		Check(std::strstr(line, "3.00 us a build") != nullptr, "the build's mean time");
	}

	return Failures();
}
