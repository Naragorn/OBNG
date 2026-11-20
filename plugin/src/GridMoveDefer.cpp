#include "GridMoveDefer.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "StepHooks.h"
#include "engine/world/GridMoveDeferLogic.h"

namespace obng::plugin {

namespace {

namespace gmd = obng::engine::world::grid_move_defer;

constexpr double kLongMs = 3.0;  // a run of this length is a crossing (the move alone is microseconds otherwise)

LogFn g_log = nullptr;
bool g_on = false;
void* g_block = nullptr;  // the block's copy, ending in a RET
double g_ticksPerMs = 0.0;
gmd::Runs g_runs;  // the main thread's only

// The copy run as the step runs the block: EBX = 0 (the step's zero register), ESI and EDI its scratch.
__declspec(naked) void __cdecl RunBlock() {
	__asm {
		push ebx
		push esi
		push edi
		push ebp
		xor ebx, ebx
		call dword ptr [g_block]
		pop ebp
		pop edi
		pop esi
		pop ebx
		ret
	}
}

// At an update step's start: the block the step would have run after the player's update.
void RunDeferred() {
	if (!g_on) {
		return;  // the jump over the block is not in: the step runs the block itself
	}
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	RunBlock();
	QueryPerformanceCounter(&t1);
	gmd::Count(g_runs, static_cast<double>(t1.QuadPart - t0.QuadPart) / g_ticksPerMs, kLongMs);
}

// The game's block copied into executable memory; null with a log line when it is not the game's own.
void* CopyBlock(LogFn log) {
	const UInt8* block = reinterpret_cast<const UInt8*>(static_cast<std::uintptr_t>(gmd::kBlockStart));
	const std::uint32_t sum = gmd::Checksum(block, gmd::kBlockSize);
	if (sum != gmd::kBlockChecksum) {
		log("Route: grid move defer - the block at %08X is not the game's own (sum %08X); not routed", gmd::kBlockStart, sum);
		return nullptr;
	}
	UInt8* copy = static_cast<UInt8*>(AllocExecutable(gmd::kBlockSize + 1));
	if (copy == nullptr) {
		log("Route: grid move defer - no executable memory; not routed");
		return nullptr;
	}
	const std::uint32_t to = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(copy));
	if (gmd::Relocate(block, gmd::kBlockSize, gmd::kBlockStart, to, gmd::kCallOffsets, gmd::kCallCount, copy, gmd::kBlockSize + 1) != gmd::Copy::Ok) {
		log("Route: grid move defer - the block's calls are not where they were read; not routed");
		return nullptr;
	}
	FlushInstructionCache(GetCurrentProcess(), copy, gmd::kBlockSize + 1);
	return copy;
}

}  // namespace

void InstallGridMoveDefer(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (gmd::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "GridMoveDefer", 0, iniPath))) == gmd::Mode::Off) {
		return;
	}
	g_log = log;
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	g_ticksPerMs = static_cast<double>(frequency.QuadPart) / 1000.0;
	g_block = CopyBlock(log);
	if (g_block == nullptr) {
		return;
	}
	if (!AddStepHook(&RunDeferred, "the grid's move with the player", log)) {
		log("Route: grid move defer - no step hook; not routed");
		return;
	}
	UInt8 jump[5];
	gmd::SkipJump(gmd::kBlockStart, gmd::kBlockEnd, jump);
	if (!SafeWrite(gmd::kBlockStart, jump, sizeof(jump))) {
		log("Route: grid move defer - the jump at %08X could not be written; not routed", gmd::kBlockStart);
		return;
	}
	g_on = true;
	log("Route: grid move defer - the grid's move with the player runs at the update step's start (the block %08X - %08X, copied to %p)",
	    gmd::kBlockStart, gmd::kBlockEnd - 1, g_block);
}

void LogGridMoveDeferSummary() {
	if (!g_on) {
		return;
	}
	g_log("Route: grid move defer - %llu runs at a step's start, %.3f ms in all, %llu of %.0f ms or more, the longest %.3f ms",
	      static_cast<unsigned long long>(g_runs.calls), g_runs.totalMs, static_cast<unsigned long long>(g_runs.longCalls), kLongMs, g_runs.longestMs);
	g_runs = gmd::Runs{};
}

}  // namespace obng::plugin
