#include "PassMaskAlternate.h"

#include <windows.h>

#include <cstdint>

#include "engine/render/PassMaskAlternateLogic.h"

namespace obng::plugin {

namespace {

namespace alt = obng::engine::render::pass_mask_alternate;

LogFn g_log = nullptr;
std::uint32_t g_bits = 0;
std::uint64_t g_switches = 0;  // the summary's thread alone

}  // namespace

void InstallPassMaskAlternate(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_bits = alt::BitsOf(static_cast<int>(GetPrivateProfileIntA("Probe", "PassMaskAlternate", alt::Shipped(), iniPath)));
	if (!alt::On(g_bits)) {
		return;
	}
	g_log = log;
	log("Probe: pass mask alternate - every summary flips the bits %02X of the lighting pass mask at %08X and so makes every pass list build anew; "
	    "the run starts with the game's mask. The picture changes: a measuring run only",
	    g_bits, alt::kPassMask);
}

void LogPassMaskAlternateSummary() {
	if (!alt::On(g_bits)) {
		return;
	}
	++g_switches;
	// One aligned word, written here and read by the render thread: a frame sees the old mask or the new one.
	auto* mask = reinterpret_cast<volatile UInt32*>(static_cast<std::uintptr_t>(alt::kPassMask));
	const UInt32 now = alt::Flip(*mask, g_bits);
	*mask = now;
	LARGE_INTEGER tick;
	QueryPerformanceCounter(&tick);
	g_log("Probe: pass mask alternate - switch %llu at tick %lld: the mask %02X from here (%s)", g_switches, tick.QuadPart, now,
	      alt::Flipped(g_switches) ? "flipped" : "the game's");
}

}  // namespace obng::plugin
