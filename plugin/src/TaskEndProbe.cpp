#include "TaskEndProbe.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "engine/io/TaskEndsLogic.h"

namespace obng::plugin {

namespace {

namespace te = obng::engine::io::task_ends;

constexpr UInt32 kEndSite = 0x00433632;  // the pump's call of a task's end: mov edx,[ecx]; mov eax,[edx+14h]; call eax (read)
constexpr UInt8 kEndBytes[] = {0x8B, 0x11, 0x8B, 0x42, 0x14, 0xFF, 0xD0};
constexpr UInt32 kRecord = 0x1C;  // a cell task's record: x, y (read)
constexpr double kLongMs = 2.0;

using ThisFn = void(__thiscall*)(void*);

LogFn g_log = nullptr;
bool g_on = false;
double g_ticksPerMs = 0.0;
std::uint64_t g_longTicks = 0;
te::Table g_table;   // the pump's thread's only (the main thread in the game, the loading thread's waits beside it
te::LongEnds g_long; // are rare; a torn count there costs one line's number)

// In place of the pump's three instructions: the task's end, timed.
void __fastcall End(void* task) {
	const UInt32 address = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(task));
	const UInt32 vtable = *reinterpret_cast<const UInt32*>(address);
	const std::int32_t a = *reinterpret_cast<const std::int32_t*>(address + kRecord);  // read before the end: it may free the task's parts
	const std::int32_t b = *reinterpret_cast<const std::int32_t*>(address + kRecord + 4);
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	reinterpret_cast<ThisFn>(*reinterpret_cast<const UInt32*>(vtable + 0x14))(task);
	QueryPerformanceCounter(&t1);
	const std::uint64_t ticks = static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
	te::Add(g_table, vtable, ticks);
	if (te::IsLong(ticks, g_longTicks)) {
		te::Keep(g_long, te::LongEnd{vtable, ticks, static_cast<std::uint64_t>(t1.QuadPart), a, b});
	}
}

}  // namespace

void InstallTaskEndProbe(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (GetPrivateProfileIntA("Probe", "TaskEnds", 0, iniPath) != 1) {
		return;
	}
	g_log = log;
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	g_ticksPerMs = static_cast<double>(frequency.QuadPart) / 1000.0;
	g_longTicks = static_cast<std::uint64_t>(kLongMs * g_ticksPerMs);
	if (!Verify(kEndSite, kEndBytes, sizeof(kEndBytes))) {
		log("Probe: task ends - the pump's call at %08X is not the game's own (a route on it?); not probed", kEndSite);
		return;
	}
	UInt8 patch[sizeof(kEndBytes)];
	std::memset(patch, 0x90, sizeof(patch));
	patch[0] = 0xE8;
	const UInt32 rel = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&End)) - (kEndSite + 5);
	std::memcpy(patch + 1, &rel, 4);
	if (!SafeWrite(kEndSite, patch, sizeof(patch))) {
		log("Probe: task ends - the pump's call at %08X could not be written; not probed", kEndSite);
		return;
	}
	g_on = true;
	log("Probe: task ends - the pump's ends of background tasks are timed by the task's vtable; ends of %.0f ms or more are listed", kLongMs);
}

void LogTaskEndSummary() {
	if (!g_on) {
		return;
	}
	const te::Table table = g_table;
	const te::LongEnds longs = g_long;
	g_table = te::Table{};
	g_long = te::LongEnds{};
	for (std::size_t i = 0; i <= te::kKinds; ++i) {
		const te::Kind& kind = i < te::kKinds ? table.kinds[i] : table.other;
		if (kind.calls == 0) {
			continue;
		}
		g_log("Probe: task ends - vtable %08X: %llu ends, %.3f ms in all, the longest %.3f ms", kind.vtable, static_cast<unsigned long long>(kind.calls),
		      static_cast<double>(kind.ticks) / g_ticksPerMs, static_cast<double>(kind.longest) / g_ticksPerMs);
	}
	for (unsigned i = 0; i < longs.count; ++i) {
		const te::LongEnd& end = longs.ends[i];
		g_log("Probe: task end - vtable %08X %.3f ms, record %d %d, ending at tick %lld", end.vtable, static_cast<double>(end.ticks) / g_ticksPerMs, end.a,
		      end.b, static_cast<long long>(end.endedAt));
	}
	if (longs.dropped != 0) {
		g_log("Probe: task ends - %u more long ends not listed", longs.dropped);
	}
}

}  // namespace obng::plugin
