#pragma once
// [Route] StereoDrawTimer (a measurement, off in play): the timers of the
// stereo draw route - the whole draw's device calls, shader slots and render
// state calls, the eye switch's steps and the pass setups' repeats - kept
// apart from the route itself (RouteStereoDraw.cpp) so that the route's file
// stays the route. Render thread only, like the route's stats.
#include <intrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "InstanceGroupLogic.h"
#include "RouteStereoDrawLogic.h"

namespace obng::plugin::draw_timer {

namespace route = stereo_draw_route;

// The words InstanceGroupLogic.h compares, read under SEH (StereoDrawTimer.cpp);
// false when a read faulted.
bool ReadInstanceWords(std::uint32_t propertyState, std::uint32_t pass, std::uint32_t* words);

struct Timers {
	bool on = false;
	std::uint64_t deviceCalls = 0;
	std::uint64_t deviceTicks = 0;
	route::StepTimes steps;
	route::PortTimes ports;
	route::PassRepeats repeats;
	// The instance count (InstanceGroupLogic.h): every eighth frame's eye-pass
	// pass setups noted and counted at the next frame's eye data.
	bool eyeDraw = false;  // the whole draw in flight draws for the eyes
	bool noteFrame = false;
	std::uint32_t frames = 0;
	std::vector<instance_groups::Key> keys;
	instance_groups::Totals instances;
	std::uint64_t unreadable = 0;  // pass setups whose words faulted (not noted)

	static constexpr std::uint32_t kGeometryDataOffset = 0xB4;     // NiGeometry's model data (xOBSE NiObjects.h geomData; read 0x00722AE5)
	static constexpr std::uint32_t kShaderPassOffset = 0x3C;       // what slot 12 0x0077A1B0 calls slot 2 of (re/export/rea-all 0x77a1b0.c)
	static constexpr std::uint32_t kEffectWordsOffset = 0x0C;      // NiDynamicEffectState: five dwords after its flag byte (xOBSE NiNodes.h, size 0x20)
	static constexpr std::uint32_t kEffectWords = 5;

	// A pass setup (slot 12) of the whole draw: its key, when this frame is noted.
	void NotePassSetup(std::uint32_t geometry, std::uint32_t shader, std::uint32_t propertyState, std::uint32_t effectState) {
		if (!noteFrame || !eyeDraw || geometry == 0 || shader == 0) {
			return;
		}
		instance_groups::Key k;
		k.data = Word(geometry + kGeometryDataOffset);
		k.pass = Word(shader + kShaderPassOffset);
		if (!ReadInstanceWords(propertyState, k.pass, k.words)) {
			++unreadable;
			return;
		}
		k.props = instance_groups::HashWords(k.words, 10);
		std::uint32_t effects[kEffectWords] = {};
		if (effectState != 0) {
			std::memcpy(effects, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(effectState + kEffectWordsOffset)), sizeof(effects));
		}
		k.effects = instance_groups::HashWords(effects, kEffectWords);
		keys.push_back(k);
	}

	// The eye data of a new frame: the noted frame before it counted.
	void FrameBoundary() {
		if (noteFrame && !keys.empty()) {
			instances.AddCoverage(instance_groups::Coverage(keys));
			instances.Add(instance_groups::Count(keys));
		}
		keys.clear();
		noteFrame = (frames++ % 8) == 0;
	}

	static std::uint32_t Word(std::uint32_t address) {
		std::uint32_t value;
		std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), sizeof(value));
		return value;
	}

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
		instance_groups::Format(instances, line, sizeof(line));
		log(line);
		if (unreadable != 0) {
			std::snprintf(line, sizeof(line), "Route: stereo draw instance groups - %llu pass setups not noted (a read faulted)", static_cast<unsigned long long>(unreadable));
			log(line);
			unreadable = 0;
		}
		deviceCalls = 0;
		deviceTicks = 0;
		steps = route::StepTimes{};
		ports = route::PortTimes{};
		repeats = route::PassRepeats{};
		instances = instance_groups::Totals{};
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
