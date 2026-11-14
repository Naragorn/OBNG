// [Route] ActorFarGlide: see ActorFarGlide.h.

#include "ActorFarGlide.h"

#include <windows.h>

#include <cstring>

#include "engine/scene/ActorFarGlideLogic.h"

namespace obng::plugin {

namespace {

namespace far_update = engine::scene::actor_far_update;
namespace glide = engine::scene::actor_far_glide;

constexpr UInt32 kRefPosition = 0x2C;      // the reference's position (ActorFarUpdate.cpp)
constexpr UInt32 kRefNode = 0x3C;          // its scene node (ActorCensus.cpp)
constexpr UInt32 kFormId = 0x0C;
constexpr UInt32 kLocalTranslate = 0x54;   // NiAVObject's local translate (SceneUpdate.h kLocalTransformOffset + 9 floats; read 0x00406950)

bool g_on = false;
LogFn g_log = nullptr;
unsigned g_samplesLeft = 0;           // [Route] ActorFarGlideSamples: lines of single updates left
volatile LONG g_shifted = 0;          // entries with an offset in their node (about: for the paths that do not look the entry up)
// The summary's counts; the actors' loop thread writes, the summary reads and clears.
volatile LONG64 g_shifts = 0;
volatile LONG64 g_replaced = 0;       // the node was placed by someone else between two shifts
volatile LONG64 g_takenBack = 0;
// Updates of moving actors, [0] with nothing shown before them, [1] with an offset shown: how many, their leaps
// (thousandths of a unit) and what the glide left of them, and how many were left under half.
volatile LONG64 g_updates[2] = {};
volatile LONG64 g_leapMilli[2] = {};
volatile LONG64 g_leftMilli[2] = {};
volatile LONG64 g_halved = 0;
volatile LONG64 g_withGain = 0;       // updates that left a gain

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }

float* LocalOf(UInt32 actor) {
	const UInt32 node = Dword(actor + kRefNode);
	return node != 0 ? reinterpret_cast<float*>(node + kLocalTranslate) : nullptr;
}

}  // namespace

void ActorFarGlideInit(bool on, unsigned samples, LogFn log) {
	g_on = on;
	g_samplesLeft = samples;
	g_log = log;
	log("Route Actor::Update (far actors): between its updates a far actor is %s", on ? "shown on its way - its node's local translate shifted by the animation's summed step so far (ActorFarGlide)"
	                                                                                 : "left standing (ActorFarGlide=0)");
}

bool ActorFarGlideAny() { return g_shifted > 0; }

namespace {

// The offset out of the node; the state's `applied` stays for the caller.
void TakeOut(far_update::Entry& entry, UInt32 actor) {
	glide::State& state = entry.glide;
	if (!state.wroteSet) {
		return;
	}
	InterlockedDecrement(&g_shifted);
	float* local = LocalOf(actor);
	float back[3];
	const float none[3] = {};
	if (glide::TakeBack(state, local != nullptr ? local : none, back) && local != nullptr) {
		local[0] += back[0];
		local[1] += back[1];
		local[2] += back[2];
		InterlockedIncrement64(&g_takenBack);
	}
}

}  // namespace

void ActorFarGlideRelease(far_update::Entry& entry, UInt32 actor) {
	TakeOut(entry, actor);
	entry.glide.applied[0] = entry.glide.applied[1] = entry.glide.applied[2] = 0.0f;
}

void ActorFarGlideSkipped(far_update::Entry& entry, UInt32 actor, float elapsed) {
	if (!g_on) {
		return;
	}
	glide::State& state = entry.glide;
	float offset[3];
	if (!glide::Offset(state, entry.hasRootMotion, entry.rootMotion, offset, elapsed)) {
		return;
	}
	float* local = LocalOf(actor);
	if (local == nullptr) {
		return;
	}
	const bool replaced = glide::RootMoved(state, local);
	float delta[3];
	if (!glide::Shift(state, offset, replaced, delta)) {
		return;
	}
	local[0] += delta[0];
	local[1] += delta[1];
	local[2] += delta[2];
	if (!state.wroteSet) {
		InterlockedIncrement(&g_shifted);
	}
	glide::Wrote(state, local);
	InterlockedIncrement64(&g_shifts);
	if (replaced) {
		InterlockedIncrement64(&g_replaced);
	}
}

void ActorFarGlideAtUpdate(far_update::Entry& entry, UInt32 actor, const float* step, float seconds) {
	if (!g_on) {
		return;
	}
	TakeOut(entry, actor);  // the offset shown until now stays in the state: AtUpdate keeps it for the measurement
	float here[3];
	std::memcpy(here, reinterpret_cast<const void*>(actor + kRefPosition), sizeof(here));
	const float moved[3] = {here[0] - entry.lastPosition[0], here[1] - entry.lastPosition[1], here[2] - entry.lastPosition[2]};
	const float none[3] = {};
	const float shown[3] = {entry.glide.shown[0], entry.glide.shown[1], entry.glide.shown[2]};
	const float consumed = entry.glide.consumed;
	const glide::Measured m = glide::AtUpdate(entry.glide, entry.hasPosition, moved, step != nullptr, step != nullptr ? step : none, seconds);
	if (m.counted) {
		const int kind = m.shown ? 1 : 0;
		InterlockedIncrement64(&g_updates[kind]);
		InterlockedExchangeAdd64(&g_leapMilli[kind], static_cast<LONG64>(m.leap * 1000.0f));
		InterlockedExchangeAdd64(&g_leftMilli[kind], static_cast<LONG64>(m.left * 1000.0f));
		if (m.shown && m.left < 0.5f * m.leap) {
			InterlockedIncrement64(&g_halved);
		}
		if (g_samplesLeft != 0) {
			--g_samplesLeft;
			g_log("Route: far actors' glide sample - actor %08X: the last update moved it %.1f %.1f %.1f (%.1f units) on a summed step of %.1f; shown before "
			      "it %.1f %.1f %.1f; left %.1f; the step it takes now %.1f, gain %s, steady speed %s",
			      Dword(actor + kFormId), static_cast<double>(moved[0]), static_cast<double>(moved[1]), static_cast<double>(moved[2]),
			      static_cast<double>(m.leap), static_cast<double>(consumed), static_cast<double>(shown[0]), static_cast<double>(shown[1]),
			      static_cast<double>(shown[2]), static_cast<double>(m.left), static_cast<double>(entry.glide.consumed), entry.glide.hasGain ? "yes" : "no",
			      entry.glide.steady ? "yes" : "no");
		}
	}
	if (entry.glide.hasGain) {
		InterlockedIncrement64(&g_withGain);
	}
}

void LogActorFarGlideSummary() {
	if (!g_on) {
		return;
	}
	const LONG64 plain = InterlockedExchange64(&g_updates[0], 0);
	const LONG64 shown = InterlockedExchange64(&g_updates[1], 0);
	const double plainLeap = static_cast<double>(InterlockedExchange64(&g_leapMilli[0], 0)) / 1000.0;
	const double leap = static_cast<double>(InterlockedExchange64(&g_leapMilli[1], 0)) / 1000.0;
	const double left = static_cast<double>(InterlockedExchange64(&g_leftMilli[1], 0)) / 1000.0;
	InterlockedExchange64(&g_leftMilli[0], 0);
	g_log("Route: far actors' glide - %lld shifts (%lld after someone else placed the node), %lld offsets taken back, %ld actors shifted now; updates of "
	      "moving actors with an offset shown before: %lld, the leap %.1f units on the mean, %.1f left of it (%.0f %%), left under half in %lld; with "
	      "none shown: %lld, the leap %.1f units; %lld updates left a gain",
	      InterlockedExchange64(&g_shifts, 0), InterlockedExchange64(&g_replaced, 0), InterlockedExchange64(&g_takenBack, 0), static_cast<long>(g_shifted),
	      shown, shown != 0 ? leap / static_cast<double>(shown) : 0.0, shown != 0 ? left / static_cast<double>(shown) : 0.0,
	      leap > 0.0 ? left * 100.0 / leap : 0.0, InterlockedExchange64(&g_halved, 0), plain,
	      plain != 0 ? plainLeap / static_cast<double>(plain) : 0.0, InterlockedExchange64(&g_withGain, 0));
}

}  // namespace obng::plugin
