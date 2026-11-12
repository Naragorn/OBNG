#pragma once
// The whole draw's device and render state port when the draw timer
// ([Route] StereoDrawTimer) or the draw instancing ([Route] DrawInstancing)
// is on (RouteStereoDraw.cpp RouteDrawPrimitive): the instancing may take an
// indexed device draw itself; everything else goes to the engine's port,
// timed when the timer is on. With both off the route uses the engine's port
// directly.
#include <cstdint>

#include "DrawInstancing.h"
#include "StereoDrawTimer.h"

namespace obng::plugin {

inline bool RoutedDeviceWanted() {
	return draw_timer::g_timers.on || draw_instancing::g_on;
}

template <class Ports, class IndexedDraw, class PlainDraw>
struct RoutedDevice {
	const Ports* inner;
	const std::uint32_t* shader;  // the whole draw's last slot-14 shader (RecordingShaderPort::lastShader)
	std::uint32_t geometry;

	draw_timer::TimedDevice<Ports, IndexedDraw, PlainDraw> Timed() const { return {inner}; }
	std::uint32_t Device() const { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(inner->renderer->device)); }

	void DrawIndexedPrimitive(const IndexedDraw& d) const {
		if (draw_instancing::g_on &&
		    draw_instancing::RouteIndexedDraw(Device(), *shader, geometry,
		                                      draw_instancing::IndexedCall{d.primitiveType, static_cast<std::int32_t>(d.baseVertexIndex), d.minVertexIndex,
		                                                                   d.vertexCount, d.startIndex, d.primitiveCount})) {
			return;
		}
		if (draw_timer::g_timers.on) {
			Timed().DrawIndexedPrimitive(d);
		} else {
			inner->DrawIndexedPrimitive(d);
		}
	}
	void DrawPrimitive(const PlainDraw& d) const {
		if (draw_instancing::g_on) {
			draw_instancing::RoutePlainDraw(geometry);
		}
		if (draw_timer::g_timers.on) {
			Timed().DrawPrimitive(d);
		} else {
			inner->DrawPrimitive(d);
		}
	}
	void SubObjectSlot1() const {
		if (draw_timer::g_timers.on) {
			Timed().SubObjectSlot1();
		} else {
			inner->SubObjectSlot1();
		}
	}
	void SetDrawFlag(std::uint32_t value) const {
		if (draw_timer::g_timers.on) {
			Timed().SetDrawFlag(value);
		} else {
			inner->SetDrawFlag(value);
		}
	}
};

}  // namespace obng::plugin
