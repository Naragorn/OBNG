// Every flow of the actor shadows' rest (src/engine/render/ActorShadowRestLogic.h).

#include "Check.h"
#include "engine/render/ActorShadowRestLogic.h"

#include <cstdio>
#include <cstring>

using namespace obng::engine::render::actor_shadow_rest;

namespace {

// A light that may rest: drawn, visible, faded in, of the actor kind.
LightView Still(std::uint32_t light, std::uint32_t caster = 0x5000) {
	LightView v;
	v.light = light;
	v.caster = caster;
	v.center[0] = 100.0f;
	v.center[1] = 200.0f;
	v.center[2] = 300.0f;
	v.map = 0x7000;
	v.visible = true;
	v.fade = 1.0f;
	v.fadeTarget = 1.0f;
	v.fadeTimer = 2.0f;
	v.fadeTime = 1.0f;
	v.actorKind = true;
	return v;
}

Settings Every(std::uint32_t every, float still = 4.0f) {
	Settings s;
	s.every = every;
	s.still = still;
	return s;
}

// One step the way the plugin runs it for one light that is registered and
// reaches the draw block: the reset's question, then the draw block's, then
// the map render's.
bool RunStep(Table& t, std::uint32_t step, const LightView& v, bool on = true, bool safe = true) {
	t.BeginStep(step, on, safe);
	const bool kept = t.Keep(v) == Why::kKept;
	const bool rested = kept && t.Rest(v.light, v.caster, v.fadeTarget);
	if (!t.Rested(v.light)) {
		t.Drawn(v);
	}
	return rested;
}

}  // namespace

int main() {
	Section("Off: every light keeps nothing");
	{
		Table t(Every(1));
		Check(!t.On(), "every 1: the rest is off");
		t.BeginStep(1, true, true);
		t.Drawn(Still(0x1000));
		t.BeginStep(2, true, true);
		Check(t.Keep(Still(0x1000)) == Why::kOff && !t.Rest(0x1000, 0x5000, 1.0f) && !t.Rested(0x1000), "every 1: off, nothing rests");
		Table zero(Every(0));
		Check(!zero.On(), "every 0: off as well");
	}

	Section("The alternation: blocks of n steps on, then n off");
	Check(OnInStep(1, 2, false) && OnInStep(2, 2, false) && OnInStep(3, 2, false), "no alternation: every step on");
	Check(OnInStep(0, 2, true) && OnInStep(1, 2, true) && !OnInStep(2, 2, true) && !OnInStep(3, 2, true) && OnInStep(4, 2, true), "every 2: two on, two off");
	Check(OnInStep(3, 4, true) && !OnInStep(4, 4, true) && !OnInStep(7, 4, true) && OnInStep(8, 4, true), "every 4: four on, four off");
	Check(!OnInStep(5, 1, true) && !OnInStep(5, 0, false), "off: never on");

	Section("A safe step: the map count unchanged and no gap");
	Check(SafeStep(3, 3, 11.0, 100.0), "same count, 11 ms since the last step: safe");
	Check(!SafeStep(5, 3, 11.0, 100.0), "the count changed (interior to exterior): the pool is resized after the reset - not safe");
	Check(!SafeStep(3, 3, 250.0, 100.0), "a gap of 250 ms (loading, a menu): not safe");
	Check(!SafeStep(3, 3, -1.0, 100.0), "no step before (gap unknown): not safe");

	Section("A still light: drawn on its turn, at rest between");
	{
		Table t(Every(2));
		const LightView v = Still(0x1000);
		Check(!RunStep(t, 1, v), "step 1: new, drawn");
		const std::uint32_t turn = t.TurnOf(0x1000);
		Check(turn < 2, "a turn in 0..n-1 was given");
		// With every 2 it rests on the steps where (step + turn) % 2 != 0.
		const bool restsAt2 = (2 + turn) % 2 != 0;
		Check(RunStep(t, 2, v) == restsAt2, "step 2: rests or draws by its turn");
		Check(RunStep(t, 3, v) == !restsAt2, "step 3: the other");
		Check(RunStep(t, 4, v) == restsAt2, "step 4: as step 2");
		Check(t.counts.rested >= 1 && t.counts.drawn[1] >= 2, "the rests and the draws counted on the on side");
	}

	Section("Every 4: at most three steps at rest in a row");
	{
		Table t(Every(4));
		const LightView v = Still(0x1000);
		RunStep(t, 1, v);
		int run = 0;
		int longest = 0;
		int draws = 0;
		for (std::uint32_t step = 2; step <= 41; ++step) {
			if (RunStep(t, step, v)) {
				++run;
				longest = run > longest ? run : longest;
			} else {
				run = 0;
				++draws;
			}
		}
		Check(longest == 3, "three rests in a row at most");
		Check(draws == 10, "forty steps: ten draws");
	}

	Section("Two lights get different turns");
	{
		Table t(Every(2));
		RunStep(t, 1, Still(0x1000));
		RunStep(t, 1, Still(0x2000, 0x6000));
		Check(t.TurnOf(0x1000) != t.TurnOf(0x2000), "the second light's turn is the next one");
		Check(t.TurnOf(0x3000) == Table::kNoTurn, "an unknown light has no turn");
	}

	Section("Why a light is not kept");
	{
		Table t(Every(4));
		const LightView v = Still(0x1000);
		t.BeginStep(1, true, true);
		Check(t.Keep(v) == Why::kNew, "never drawn: new");
		t.Drawn(v);
		const std::uint32_t turn = t.TurnOf(0x1000);
		// Each refusal below leaves the light drawn again in the same step, so
		// the next question still finds it seen in the step before; a step on
		// its turn is run as a plain draw first.
		std::uint32_t step = 1;
		auto ask = [&](const LightView& view, bool on, bool safe) {
			++step;
			if ((step + turn) % 4 == 0) {
				t.BeginStep(step, true, true);
				t.Drawn(Still(0x1000));
				++step;
			}
			t.BeginStep(step, on, safe);
			const Why why = t.Keep(view);
			t.Drawn(Still(0x1000));
			return why;
		};
		Check(ask(v, false, true) == Why::kOff, "the step is off (the alternation): off");
		Check(ask(v, true, false) == Why::kUnsafe, "the step is not safe: unsafe");
		LightView other = v;
		other.actorKind = false;
		Check(ask(other, true, true) == Why::kOtherKind, "not a light the caster registration made: other kind");
		other = v;
		other.caster = 0x9000;
		Check(ask(other, true, true) == Why::kCasterChanged, "another caster: changed");
		other = v;
		other.map = 0;
		Check(ask(other, true, true) == Why::kNoMap, "no map held: no map");
		other = v;
		other.visible = false;
		Check(ask(other, true, true) == Why::kNotVisible, "not in the view last time: not visible");
		other = v;
		other.fade = 0.04f;
		Check(ask(other, true, true) == Why::kFaint, "fade below 0.05: faint");
		other = v;
		other.fadeTarget = 0.0f;
		Check(ask(other, true, true) == Why::kFading, "fading out: fading");
		other = v;
		other.fadeTimer = 0.5f;
		Check(ask(other, true, true) == Why::kFading, "fading in (timer below the fade time): fading");
		other = v;
		other.center[0] += 5.0f;
		Check(ask(other, true, true) == Why::kMoved, "moved 5 units with still 4: moved");
		other = v;
		other.center[2] += 3.9f;
		Check(ask(other, true, true) == Why::kKept, "moved 3.9 units: kept");
		Check(t.counts.notKept[static_cast<int>(Why::kMoved)] == 1 && t.counts.notKept[static_cast<int>(Why::kNew)] == 1, "the reasons counted");
		// Its turn: the step after a draw where (step + turn) % 4 == 0.
		std::uint32_t turnStep = step + 1;
		while ((turnStep + turn) % 4 != 0) {
			++turnStep;
		}
		t.BeginStep(turnStep - 1, true, true);
		t.Drawn(v);
		t.BeginStep(turnStep, true, true);
		Check(t.Keep(v) == Why::kTurn, "its turn: drawn");
	}

	Section("Not seen in the step before: new again");
	{
		Table t(Every(2));
		const LightView v = Still(0x1000);
		RunStep(t, 1, v);
		t.BeginStep(3, true, true);
		Check(t.Keep(v) == Why::kNew, "skipped a step: its map may be stale - new");
	}

	Section("The draw block: only a kept light with its caster rests");
	{
		Table t(Every(2));
		const LightView v = Still(0x1000);
		RunStep(t, 1, v);
		std::uint32_t step = 2;
		while ((step + t.TurnOf(0x1000)) % 2 == 0) {
			++step;
		}
		t.BeginStep(step - 1, true, true);
		t.Drawn(v);
		t.BeginStep(step, true, true);
		Check(!t.Rest(0x1000, 0x5000, 1.0f), "not kept at the reset: not at rest");
		Check(t.Keep(v) == Why::kKept, "kept");
		Check(!t.Rest(0x1000, 0x9999, 1.0f), "another caster at the draw block: drawn");
		Check(!t.Rest(0x2000, 0x5000, 1.0f), "an unknown light: drawn");
		Check(!t.Rest(0x1000, 0x5000, 0.0f) && t.counts.fadingAtDraw == 1,
		      "its caster not registered: the walk shows it to fade it out (target 0) - drawn, so the fade goes on");
		Check(t.Rest(0x1000, 0x5000, 1.0f) && t.Rested(0x1000), "its caster: at rest");
		Check(!t.Rested(0x2000), "another light is not at rest");
		std::uint32_t out[4] = {};
		Check(t.Unrested(out, 4) == 0, "kept and at rest: nothing to release");
	}

	Section("Kept but not at rest (the caster not registered, or the count reached): released before the map render");
	{
		Table t(Every(2));
		const LightView a = Still(0x1000);
		const LightView b = Still(0x2000, 0x6000);
		t.BeginStep(1, true, true);
		t.Drawn(a);
		t.Drawn(b);
		// A step where both may rest.
		std::uint32_t step = 2;
		for (; step < 20; ++step) {
			if ((step + t.TurnOf(0x1000)) % 2 != 0 && (step + t.TurnOf(0x2000)) % 2 != 0) {
				break;
			}
			t.BeginStep(step, true, true);
			t.Drawn(a);
			t.Drawn(b);
		}
		// Every 2 and two lights: their turns differ, so no step lets both rest.
		Check(step == 20, "two lights at every 2: never both at rest in one step");
		Table u(Every(3));
		u.BeginStep(1, true, true);
		u.Drawn(a);
		u.Drawn(b);
		std::uint32_t s = 2;
		for (; s < 20; ++s) {
			if ((s + u.TurnOf(0x1000)) % 3 != 0 && (s + u.TurnOf(0x2000)) % 3 != 0) {
				break;
			}
			u.BeginStep(s, true, true);
			u.Drawn(a);
			u.Drawn(b);
		}
		Check(s < 20, "every 3: a step where both may rest");
		u.BeginStep(s, true, true);
		Check(u.Keep(a) == Why::kKept && u.Keep(b) == Why::kKept, "both kept at the reset");
		Check(u.Rest(0x1000, 0x5000, 1.0f), "the first reaches the draw block and rests");
		std::uint32_t out[1] = {};
		Check(u.Unrested(out, 1) == 1 && out[0] == 0x2000, "the second did not: to release");
		u.Released(0x2000);
		Check(u.counts.released == 1, "the release counted");
		Check(u.Unrested(out, 1) == 0, "released: no longer named");
		u.BeginStep(s + 1, true, true);
		Check(u.Keep(b) == Why::kNew, "a released light is not seen in the step before: new");
	}

	Section("A drawn light without a map after the render is not noted as drawn");
	{
		Table t(Every(2));
		LightView v = Still(0x1000);
		t.BeginStep(1, true, true);
		v.map = 0;
		t.Drawn(v);
		Check(t.TurnOf(0x1000) == Table::kNoTurn && t.counts.drawn[1] == 0, "no map: not noted, not counted");
	}

	Section("The table: full, and slots of lights gone taken over");
	{
		Table t(Every(2));
		t.BeginStep(1, true, true);
		for (std::uint32_t i = 0; i < Table::kSlots; ++i) {
			t.Drawn(Still(0x1000 + i * 0x10));
		}
		t.Drawn(Still(0x9000));
		Check(t.TurnOf(0x9000) == Table::kNoTurn && t.counts.noSlot == 1, "full in one step: the next light gets no slot");
		t.BeginStep(3, true, true);
		t.Drawn(Still(0x9000));
		Check(t.TurnOf(0x9000) != Table::kNoTurn, "two steps later the old slots are free again");
	}

	Section("The counts and the summary line");
	{
		Table t(Every(2, 4.0f));
		RunStep(t, 1, Still(0x1000));
		RunStep(t, 2, Still(0x1000));
		RunStep(t, 3, Still(0x1000));
		t.AddStepTicks(true, 300);
		t.AddStepTicks(false, 500);
		Check(t.counts.steps[1] == 3 && t.counts.steps[0] == 0, "three on steps");
		Check(t.counts.stepTicks[1] == 300 && t.counts.stepTicks[0] == 500, "the ticks by side");
		RunStep(t, 4, Still(0x1000), false);
		Check(t.counts.steps[0] == 1 && t.counts.drawn[0] == 1, "an off step: counted on the off side");
		char line[1024];
		FormatSummary(t.counts, t.settings(), 1000.0, line, sizeof(line));
		Check(std::strstr(line, "every 2. step") != nullptr && std::strstr(line, "within 4.0 units") != nullptr, "the line names the settings");
		Check(std::strstr(line, "steps 3 on / 1 off") != nullptr, "and the steps by side");
		std::printf("    %s\n", line);
		t.ResetCounts();
		Check(t.counts.steps[1] == 0 && t.counts.rested == 0, "the counts reset for the next window");
	}

	return Failures();
}
