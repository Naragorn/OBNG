#pragma once
// [Route] StereoDrawTimer (a measurement, off in play): the timers of the
// stereo draw route - the whole draw's device calls, shader slots and render
// state calls, the eye switch's steps and the pass setups' repeats - kept
// apart from the route itself (RouteStereoDraw.cpp) so that the route's file
// stays the route. Render thread only, like the route's stats.
#include <intrin.h>

#include <cstdint>

#include "RouteStereoDrawLogic.h"

namespace obng::plugin::draw_timer {

namespace route = stereo_draw_route;

struct Timers {
	bool on = false;
	std::uint64_t deviceCalls = 0;
	std::uint64_t deviceTicks = 0;
	route::StepTimes steps;
	route::PortTimes ports;
	route::PassRepeats repeats;

	// The summary's lines, and the counts started again.
	template <class Log>
	void LogAndReset(double ticksPerMillisecond, Log&& log) {
		char line[1400];
		route::FormatDeviceDraws(deviceCalls, deviceTicks, ticksPerMillisecond, line, sizeof(line));
		log(line);
		route::FormatStepTimes(steps, ticksPerMillisecond, line, sizeof(line));
		log(line);
		route::FormatPortTimes(ports, ticksPerMillisecond, line, sizeof(line));
		log(line);
		route::FormatPassRepeats(repeats, line, sizeof(line));
		log(line);
		const bool keep = on;
		*this = Timers{};
		on = keep;
	}
};

inline Timers g_timers;

// A step of the eye switch, timed while it is in scope.
class StepTimer {
public:
	explicit StepTimer(route::PortTimes::Step step) : step_(step), t0_(g_timers.on ? __rdtsc() : 0) {}
	~StepTimer() {
		if (t0_ != 0) {
			g_timers.ports.Add(step_, __rdtsc() - t0_);
		}
	}
	StepTimer(const StepTimer&) = delete;
	StepTimer& operator=(const StepTimer&) = delete;

private:
	route::PortTimes::Step step_;
	std::uint64_t t0_;
};

// The whole draw's device and render state port with a timer around each call.
template <class Ports, class IndexedDraw, class PlainDraw>
struct TimedDevice {
	const Ports* inner;
	void DrawIndexedPrimitive(const IndexedDraw& d) const {
		const std::uint64_t t0 = __rdtsc();
		inner->DrawIndexedPrimitive(d);
		g_timers.deviceTicks += __rdtsc() - t0;
		++g_timers.deviceCalls;
	}
	void DrawPrimitive(const PlainDraw& d) const {
		const std::uint64_t t0 = __rdtsc();
		inner->DrawPrimitive(d);
		g_timers.deviceTicks += __rdtsc() - t0;
		++g_timers.deviceCalls;
	}
	void SubObjectSlot1() const {
		const std::uint64_t t0 = __rdtsc();
		inner->SubObjectSlot1();
		g_timers.steps.stateTicks += __rdtsc() - t0;
		++g_timers.steps.stateCalls;
	}
	void SetDrawFlag(std::uint32_t value) const {
		const std::uint64_t t0 = __rdtsc();
		inner->SetDrawFlag(value);
		g_timers.steps.stateTicks += __rdtsc() - t0;
		++g_timers.steps.stateCalls;
	}
};

}  // namespace obng::plugin::draw_timer
