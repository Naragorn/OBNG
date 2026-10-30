#pragma once

// The range walk's route: the game RUNS BatchedWalk
// (src/engine/render/ShadowLightRangeWalkLogic.h) at the entry of
// ShadowSceneLight::CollectGeometryInRange 0x007D59E0 (proposed name) instead
// of the original, so the replacement's cost can be measured against the
// original's. The decisions and the flow over the memory port, so every flow
// runs in a test (plugin/tests/RouteRangeWalkLogicTest.cpp); RouteRangeWalk.cpp
// supplies the port and the detour.
//
// What it rests on: the RangeWalk shadow (ShadowRangeWalkLogic.h) compared
// BatchedWalk with the original in the game over the calls routed here - the
// outermost call per thread with the gate on, an object and a source - with
// 0 mismatches in the AddGeometry sequence (geometry and live +0x1B0), +0x1B0
// afterwards and AL (measured, artifacts/shadow/obng-range-walk/
// 20261030-120624/OBNG.log). Every other call goes to the original unchanged.
//
// Per outermost call on a thread with the gate on, an object and a source:
//   1. the light is read once (CaptureLight), with the rounding control of
//      the x87 control word at the entry, under which the original would
//      have walked (measured 0x0C7F, truncate, in the shadow pass);
//   2. BatchedWalk runs over the game's scene through PerformingScene: the
//      same reads and engine calls as the shadow's MemoryScene, but
//      AddGeometry 0x007D5790 is PERFORMED, in order, with +0x1B0 written
//      first to the mask the original holds there at that call (the value
//      the shadow recorded live and compared);
//   3. +0x1B0 is written back to its entry value when it was written at all:
//      the original restores its saved mask on every exit after the gate
//      (C3 of re/functions/0x007D59E0.md, read), so it leaves +0x1B0 as it
//      found it (derived);
//   4. EAX is AL: 1 when the walk ran, 0 when the gate read 0 at the walk's
//      root read (C1, read); the upper bits are 0 (the original leaves
//      whatever EAX held - see RouteEax).
//
// Marks (AGENTS.md): read = seen in the instructions (address given),
// derived = follows from read facts, measured = seen in a game run,
// unverified = not checked.
//
// 64-bit clean: addresses are std::uint32_t values the memory port resolves.

#include "ShadowRangeWalkLogic.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace obng::plugin::range_walk {

// Memory port, beyond what ShadowRangeWalkLogic.h lists:
//   W32(address, value)   a write of the game's memory (only +0x1B0 of the light)

// AL as the route returns it, in EAX. The original's upper 24 bits are not
// defined (C1, read: the gate exit never writes EAX before XOR AL,AL; the
// walked exits MOV AL,1 over what the last call left). Its callers test no
// result: the walk's own recursive calls (C2, read) and 0x007D6900 (a void
// function in the rea-all pseudocode; unverified at instruction level). So
// 0 in the upper bits is as good as any (derived, conditional on that).
constexpr std::uint32_t RouteEax(bool walked) {
	return walked ? 1u : 0u;
}

// The scene port of the route: MemoryScene's reads and engine calls, with
// AddGeometry performed on the light instead of recorded. perform(light,
// geometry) is 0x007D5790 (thiscall on the light, one stack argument, its
// result unused: no TEST after 0x007D5AFC, C2 read).
template <class Memory, class PerformAddGeometry>
struct PerformingScene : MemoryScene<Memory> {
	std::uint32_t light;
	PerformAddGeometry* perform;
	std::uint32_t adds = 0;
	bool maskWritten = false;

	// The original's +0x1B0 at its AddGeometry call is the mask IsInRange left
	// for that geometry (its CALL 0x007D5AFC comes after the narrowing and
	// before the restore; read, B14 and C3). BatchedWalk hands exactly that
	// mask (the in-game gate compared it with the live value at every call,
	// measured). AddGeometry's own instructions hold no +0x1B0 operand (read,
	// re/export/range-callers/0x007D5790/asm.s); its callees are not read, so
	// the value is put where the original has it.
	void addGeometry(ObjectHandle geometry, std::uint32_t mask) {
		this->memory.W32(light + walk_engine::kActivePlanesOffset, mask);
		maskWritten = true;
		++adds;
		(*perform)(light, geometry);
	}
};

struct RouteReport {
	EntryAction action;         // Compare: routed; a Skip*: left to the original (PassThrough is never reported)
	walk::SourceMode mode;      // routed only
	std::uint32_t adds;         // AddGeometry calls performed (routed only)
	bool result;                // AL returned (routed only)
	std::uint16_t controlWord;  // the x87 control word at the walk's entry (FNSTCW)
};

// What the route did since the last summary.
struct RouteStats {
	std::uint64_t routed[4] = {0, 0, 0, 0};  // routed walks by walk::SourceMode
	std::uint64_t byRounding[4] = {0, 0, 0, 0};  // routed walks by the rounding control at entry (which_side::X87Rounding)
	std::uint64_t adds = 0;                  // AddGeometry calls performed by routed walks
	std::uint64_t leftGateOff = 0;
	std::uint64_t leftNullObject = 0;
	std::uint64_t leftNoSource = 0;

	void Add(const RouteReport& report) {
		switch (report.action) {
		case EntryAction::Compare:
			++routed[static_cast<unsigned>(report.mode)];
			++byRounding[static_cast<unsigned>(which_side::RoundingFromControlWord(report.controlWord))];
			adds += report.adds;
			return;
		case EntryAction::SkipGateOff:
			++leftGateOff;
			return;
		case EntryAction::SkipNullObject:
			++leftNullObject;
			return;
		case EntryAction::SkipNoSource:
			++leftNoSource;
			return;
		case EntryAction::PassThrough:
			return;
		}
	}
	std::uint64_t Routed() const { return routed[0] + routed[1] + routed[2] + routed[3]; }
	std::uint64_t Left() const { return leftGateOff + leftNullObject + leftNoSource; }
	void Reset() { *this = RouteStats{}; }
};

inline void FormatRouteStats(const RouteStats& s, char* out, std::size_t capacity) {
	if (capacity == 0) {
		return;
	}
	std::snprintf(out, capacity,
		"Route: range walk %llu walks routed, %llu AddGeometry calls; by source planes %llu, distance %llu, "
		"directional %llu; %llu left to the original (gate off %llu, null object %llu, no source %llu); by rounding "
		"control at entry nearest %llu, down %llu, up %llu, truncate %llu",
		static_cast<unsigned long long>(s.Routed()), static_cast<unsigned long long>(s.adds),
		static_cast<unsigned long long>(s.routed[static_cast<unsigned>(walk::SourceMode::Planes)]),
		static_cast<unsigned long long>(s.routed[static_cast<unsigned>(walk::SourceMode::Distance)]),
		static_cast<unsigned long long>(s.routed[static_cast<unsigned>(walk::SourceMode::Directional)]),
		static_cast<unsigned long long>(s.Left()), static_cast<unsigned long long>(s.leftGateOff),
		static_cast<unsigned long long>(s.leftNullObject), static_cast<unsigned long long>(s.leftNoSource),
		static_cast<unsigned long long>(s.byRounding[0]), static_cast<unsigned long long>(s.byRounding[1]),
		static_cast<unsigned long long>(s.byRounding[2]), static_cast<unsigned long long>(s.byRounding[3]));
}

// One call of the patched entry 0x007D59E0 under the route.
//   controlWord                       the x87 control word at the entry (the detour's FNSTCW): BatchedWalk
//                                     rounds under its rounding control, as the original would
//   callOriginal(light, object)       the original through the trampoline; its EAX is returned whole
//   performAddGeometry(light, geom)   0x007D5790
//   squareRoot                        the 0x00982C30 port
//   report(RouteReport)               once per outermost call, never for an inner one
// The calls the shadow did not compare (DecideEntry's skips) go to the
// original: they are exact there and the route has no evidence for them. An
// inner call - the original's own recursion while it runs a left call, or a
// walk entered again from inside AddGeometry on this thread - goes to the
// original without reading anything (the depth, as in the shadow).
template <class Memory, class CallOriginal, class PerformAddGeometry, class SquareRoot, class Report>
std::uint32_t RouteWalk(Memory& memory, ThreadWalkState& state, walk::BatchedWalkScratch& scratch, std::uint16_t controlWord,
	std::uint32_t light, ObjectHandle object, CallOriginal&& callOriginal, PerformAddGeometry&& performAddGeometry,
	SquareRoot&& squareRoot, Report&& report) {
	const DepthScope scope(state);
	if (scope.Before() != 0) {
		return callOriginal(light, object);
	}
	const std::uint32_t gate = memory.U32(walk_engine::kWalkGateAddress);
	const std::uint32_t source = memory.U32(light + walk_engine::kSourceLightOffset);
	const EntryAction action = DecideEntry(scope.Before(), gate, object, source);
	if (action != EntryAction::Compare) {
		const std::uint32_t eax = callOriginal(light, object);
		report(RouteReport{action, walk::SourceMode::NoSource, 0, false, controlWord});
		return eax;
	}

	const walk::LightValues values = CaptureLight(memory, light, which_side::RoundingFromControlWord(controlWord));
	using Perform = std::remove_reference_t<PerformAddGeometry>;
	PerformingScene<Memory, Perform> scene{{memory, nullptr}, light, &performAddGeometry};
	walk::NoWalkObserver observer;
	const bool walked = walk::BatchedWalk(values, scene, object, squareRoot, scratch, observer);
	if (scene.maskWritten) {
		memory.W32(light + walk_engine::kActivePlanesOffset, values.activePlanes);
	}
	const walk::SourceMode mode = walk::SelectSourceMode(values.hasSource, values.sourceRtti, values.planeSource);
	report(RouteReport{action, mode, scene.adds, walked, controlWord});
	return RouteEax(walked);
}

}  // namespace obng::plugin::range_walk
