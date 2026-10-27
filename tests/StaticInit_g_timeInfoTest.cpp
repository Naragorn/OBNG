// Tests of the rebuild of 0x009DC2A0 (proposed StaticInit_g_timeInfo), the
// initialiser that builds the main loop's TimeInfo at 0x00B33E90 with
// GetTickCount() as its start tick.
//
// The body is straight-line code: no conditional jump, no computed jump
// (data.json jump_tables is empty), one RET at 0x009DC2CD (read). Its flows:
//   F1 the only path: GetTickCount (call at 0x009DC2A0), then the stores
//      +0x00 byte 0 (0x009DC2B6), +0x04/+0x08/+0x0C float +0.0 (FLDZ 0x009DC2A6,
//      FST 0x009DC2A8 / 0x009DC2B0, FSTP 0x009DC2BC), +0x10 dword 0 (0x009DC2C2),
//      +0x14 the tick (0x009DC2C8) (read). +0x01..+0x03 are not written (read).
//   F1 differential: the bytes it leaves equal the bytes the constructor
//      0x0047D150 leaves for the same tick on the same prefill (derived, C6 of
//      re/functions/0x009DC2A0.md). The constructor has no rebuild yet; the
//      reference below transcribes its seven stores from
//      re/export/time-info/0x0047D150/asm.s.
// There is no refusal or fallback path: no branch exists (read), GetTickCount
// has no failure return the body could test (no read of EAX other than the
// store at 0x009DC2C8, read), and the tick is stored verbatim whatever its value
// (0, 0xFFFFFFFF are covered below).
//
// x87 precision: not involved. FLDZ loads an exact +0.0 and FST/FSTP of +0.0
// to m32 stores 0x00000000 under every precision-control and rounding setting
// (derived, x87 semantics; C9). The tests compare bit patterns, not values, so
// a -0.0 or a NaN in the prefill must also come out as 0x00000000.

#include "Check.h"
#include "engine/time/StaticInit_g_timeInfo.h"
#include "engine/time/StaticInit_g_timeInfoLogic.h"
#include "engine/time/TimeInfo.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace logic = obng::engine::time::static_init_g_time_info;
namespace engine = obng::engine::time::static_init_g_time_info::engine;
using obng::engine::time::TimeInfo;

namespace {

constexpr std::size_t kTimeInfoSize = sizeof(TimeInfo);  // 0x18
using Bytes = std::array<std::uint8_t, kTimeInfoSize>;

Bytes BytesOf(const TimeInfo& timeInfo) {
	Bytes bytes{};
	std::memcpy(bytes.data(), &timeInfo, kTimeInfoSize);
	return bytes;
}

TimeInfo FromBytes(const Bytes& bytes) {
	TimeInfo timeInfo{};
	std::memcpy(&timeInfo, bytes.data(), kTimeInfoSize);
	return timeInfo;
}

std::uint32_t U32At(const Bytes& bytes, std::size_t offset) {
	std::uint32_t value = 0;
	std::memcpy(&value, bytes.data() + offset, sizeof(value));
	return value;
}

void PutU32At(Bytes& bytes, std::size_t offset, std::uint32_t value) {
	std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

// A non-zero pattern in every byte: 0xA0 + offset, so a byte that is not
// written keeps a value that tells which byte it is.
Bytes CountingPrefill() {
	Bytes bytes{};
	for (std::size_t i = 0; i < kTimeInfoSize; ++i) {
		bytes[i] = static_cast<std::uint8_t>(0xA0 + i);
	}
	return bytes;
}

// Floats whose bit patterns are not +0.0: -0.0 at +0x04, a quiet NaN at
// +0x08, -1.0 at +0x0C - each must still come out as 0x00000000.
Bytes FloatTrapPrefill() {
	Bytes bytes = CountingPrefill();
	PutU32At(bytes, 0x04, 0x80000000u);
	PutU32At(bytes, 0x08, 0x7FC00000u);
	PutU32At(bytes, 0x0C, 0xBF800000u);
	return bytes;
}

// The constructor 0x0047D150 (thiscall(tick), ret 4), transcribed store by
// store from re/export/time-info/0x0047D150/asm.s, as the differential's
// reference. Independent of the rebuild under test.
Bytes ConstructorReference(Bytes bytes, std::uint32_t tick) {
	PutU32At(bytes, 0x04, 0x00000000u);  // FLDZ 0x0047D150, FST [ECX+0x4] 0x0047D156
	bytes[0x00] = 0x00;                  // MOV byte ptr [ECX],0x0 0x0047D159
	PutU32At(bytes, 0x08, 0x00000000u);  // FST [ECX+0x8] 0x0047D15C
	PutU32At(bytes, 0x10, 0x00000000u);  // MOV dword ptr [ECX+0x10],0x0 0x0047D15F
	PutU32At(bytes, 0x0C, 0x00000000u);  // FSTP [ECX+0xC] 0x0047D166
	PutU32At(bytes, 0x14, tick);         // MOV [ECX+0x14],EAX 0x0047D169
	return bytes;
}

// The tick port: counts calls and records what the target held when it was
// called, to check that GetTickCount runs before any store.
struct FakeClock {
	std::uint32_t tick = 0;
	int calls = 0;
	const TimeInfo* target = nullptr;
	Bytes targetAtCall{};

	std::uint32_t GetTickCount() {
		++calls;
		if (target != nullptr) {
			targetAtCall = BytesOf(*target);
		}
		return tick;
	}
};

// The struct inside guard bytes, to see that nothing outside +0x00..+0x17 is written.
struct Guarded {
	std::array<std::uint8_t, 8> before;
	TimeInfo timeInfo;
	std::array<std::uint8_t, 8> after;
};

void CheckF1Stores(const Bytes& prefill, const Bytes& result, std::uint32_t tick) {
	Check(result[0x00] == 0x00, "+0x00 freezeDepth is the byte 0 (MOV byte ptr [0x00B33E90],CL at 0x009DC2B6, CL = 0 from 0x009DC2AE)");
	Check(result[0x01] == prefill[0x01] && result[0x02] == prefill[0x02] && result[0x03] == prefill[0x03],
		"+0x01..+0x03 keep the prefill (no store to 0x00B33E91..0x00B33E93)");
	Check(U32At(result, 0x04) == 0x00000000u, "+0x04 fixedStepMs is the bit pattern 0x00000000 (FST at 0x009DC2A8)");
	Check(U32At(result, 0x08) == 0x00000000u, "+0x08 fixedStepCarryMs is the bit pattern 0x00000000 (FST at 0x009DC2B0)");
	Check(U32At(result, 0x0C) == 0x00000000u, "+0x0C frameSeconds is the bit pattern 0x00000000 (FSTP at 0x009DC2BC)");
	Check(U32At(result, 0x10) == 0x00000000u, "+0x10 clockMs is the dword 0 (MOV dword ptr [0x00B33EA0],ECX at 0x009DC2C2)");
	Check(U32At(result, 0x14) == tick, "+0x14 startTick is GetTickCount's result, verbatim (MOV [0x00B33EA4],EAX at 0x009DC2C8)");
}

void F1PureStores() {
	Section("F1 the only path (pure): the stores over a non-zero prefill");
	const std::uint32_t tick = 0x12345678u;
	const Bytes prefill = CountingPrefill();
	const Bytes result = BytesOf(logic::ConstructedTimeInfo(FromBytes(prefill), tick));
	CheckF1Stores(prefill, result, tick);
}

void F1FloatBitPatterns() {
	Section("F1 the only path (pure): -0.0, NaN and -1.0 in the float fields all become +0.0 (FLDZ)");
	const std::uint32_t tick = 1000u;
	const Bytes prefill = FloatTrapPrefill();
	const Bytes result = BytesOf(logic::ConstructedTimeInfo(FromBytes(prefill), tick));
	CheckF1Stores(prefill, result, tick);
}

void F1TickExtremes() {
	Section("F1 the only path (pure): the tick is stored verbatim at its extremes - no clamp, no conversion");
	const Bytes prefill = CountingPrefill();
	const Bytes zero = BytesOf(logic::ConstructedTimeInfo(FromBytes(prefill), 0u));
	Check(U32At(zero, 0x14) == 0u, "tick 0 -> +0x14 = 0");
	const std::uint32_t maxTick = std::numeric_limits<std::uint32_t>::max();
	const Bytes wrapped = BytesOf(logic::ConstructedTimeInfo(FromBytes(prefill), maxTick));
	Check(U32At(wrapped, 0x14) == 0xFFFFFFFFu, "tick 0xFFFFFFFF (GetTickCount just before its wrap) -> +0x14 = 0xFFFFFFFF");
	Check(U32At(wrapped, 0x10) == 0u, "+0x10 stays 0 whatever the tick (ECX, not EAX, at 0x009DC2C2)");
}

void F1RunWithPort() {
	Section("F1 the only path (run): one GetTickCount call, before any store, then the stores");
	Guarded guarded{};
	guarded.before.fill(0x5A);
	guarded.after.fill(0xA5);
	const Bytes prefill = CountingPrefill();
	guarded.timeInfo = FromBytes(prefill);

	FakeClock clock;
	clock.tick = 0xCAFEF00Du;
	clock.target = &guarded.timeInfo;
	logic::RunStaticInit(clock, guarded.timeInfo);

	Check(clock.calls == 1, "GetTickCount is called exactly once (the only call, 0x009DC2A0)");
	Check(clock.targetAtCall == prefill, "the target is untouched when GetTickCount runs (the call precedes every store)");
	CheckF1Stores(prefill, BytesOf(guarded.timeInfo), clock.tick);

	bool guardsKept = true;
	for (std::uint8_t byte : guarded.before) {
		guardsKept = guardsKept && byte == 0x5A;
	}
	for (std::uint8_t byte : guarded.after) {
		guardsKept = guardsKept && byte == 0xA5;
	}
	Check(guardsKept, "nothing outside +0x00..+0x17 is written (highest store ends at 0x00B33EA7)");
}

void F1Differential() {
	Section("F1 differential: the same bytes as the constructor 0x0047D150 with the same tick on the same prefill");
	const std::uint32_t ticks[] = {0u, 1u, 0x12345678u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
	const Bytes prefills[] = {CountingPrefill(), FloatTrapPrefill(), Bytes{}};
	bool allEqual = true;
	for (const Bytes& prefill : prefills) {
		for (std::uint32_t tick : ticks) {
			FakeClock clock;
			clock.tick = tick;
			TimeInfo timeInfo = FromBytes(prefill);
			logic::RunStaticInit(clock, timeInfo);
			allEqual = allEqual && BytesOf(timeInfo) == ConstructorReference(prefill, tick);
		}
	}
	Check(allEqual, "all 18 prefill/tick pairs leave byte-identical 0x18 bytes");
}

void F1OnTheImageBytes() {
	Section("F1 on the image's own bytes: 0x00B33E90..0x00B33EA7 are zero before it runs (data.json, C12)");
	const std::uint32_t tick = 0x00010000u;
	const Bytes result = BytesOf(logic::ConstructedTimeInfo(FromBytes(Bytes{}), tick));
	Bytes expected{};
	PutU32At(expected, 0x14, tick);
	Check(result == expected, "all zero except +0x14 = the tick");
}

void EngineFacingShape() {
	Section("Engine-facing shape: addresses and the original's signature");
	Check(engine::kStaticInitAddress == 0x009DC2A0u, "the initialiser is 0x009DC2A0");
	Check(engine::kTargetAddress == 0x00B33E90u, "it writes g_timeInfo 0x00B33E90 (byte store at 0x009DC2B6, xOBSE g_timeInfo)");
	Check(engine::kTargetAddress + offsetof(TimeInfo, frameSeconds) == 0x00B33E9Cu,
		"frameSeconds lands at 0x00B33E9C (FSTP at 0x009DC2BC; OBVR kFrameSecondsAddress)");
	Check(engine::kTargetAddress + offsetof(TimeInfo, clockMs) == 0x00B33EA0u, "clockMs lands at 0x00B33EA0 (0x009DC2C2)");
	Check(engine::kTargetAddress + offsetof(TimeInfo, startTick) == 0x00B33EA4u, "startTick lands at 0x00B33EA4 (0x009DC2C8)");
	engine::StaticInitFunction function = nullptr;
	Check(function == nullptr, "compile check only: StaticInitFunction names the original's shape void __cdecl (void)");
}

} // namespace

int main() {
	F1PureStores();
	F1FloatBitPatterns();
	F1TickExtremes();
	F1RunWithPort();
	F1Differential();
	F1OnTheImageBytes();
	EngineFacingShape();
	return Failures();
}
