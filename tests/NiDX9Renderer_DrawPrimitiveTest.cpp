// The flows of 0x007672F0 (proposed NiDX9Renderer::DrawPrimitive): the pure
// decisions B1..B9 of NiDX9Renderer_DrawPrimitiveLogic.h, the whole draw over
// recording ports (one eye: the original's sequence; two eyes: the contract
// of StereoDraw.h), the refusals, the differential gate against the
// original's emulator trace, and the engine ports of NiDX9Renderer_
// DrawPrimitive.h over native stand-ins with the original's calling
// conventions.
//
// The differential gate, layer 1 (docs/re-workflow.md, "the gate outside the
// workflow"). The original ran in the emulator (tools/difftest/emulate.py) on
// the synthetic inputs of tools/difftest/cases/0x007672F0.json; its result is
// tools/difftest/expected/0x007672F0.json (2026-11-04: every case returned to
// its sentinel with stack_delta +24 - the RET 0x18 popping the six arguments
// - and no emulator error). Every virtual slot and the cast are traced stubs,
// so each trace is the original's complete call sequence: callee, return
// address, ECX and six stack dwords. The shader slots with seven or eight
// arguments and DrawIndexedPrimitive (seven dwords) are raw-code stubs that
// copy their seventh (and eighth) dword to a capture address; those copies
// are the only recorded writes. Trace and writes are transcribed below,
// literally, one entry per case (measured). The rebuild runs the same inputs
// over fake ports that hand out the same addresses as handles; each port call
// is expressed in the emulator's terms (stub, return address of the
// original's CALL, ECX, arguments) and the two sequences must be equal.
//
// What it compares, per case:
//   - every call in order: callee, return address, ECX (not for the cdecl cast
//     and the COM device calls, whose ECX is not an input: kAny), and every
//     argument the callee takes (the first six from the trace, the seventh
//     and eighth from the capture writes);
//   - that the body itself wrote nothing outside its stack: every recorded
//     write comes from a stub's capture store (map: "No write to any field");
//   - the path: the original's EAX at the exit is the last callee's (0xFC from
//     the draw-flag stub when the pipeline closed, slot 10's value on B3), so
//     it pins the rebuild's Outcome;
//   - with two eyes, the same trace with each pass's draws repeated between
//     Switch(1) and Switch(0) right before slot 16 (StereoDraw.h), derived
//     from the original's trace, not from the rebuild.
//
// What it cannot see:
//   - ECX of the sub-object slot-1 call and of the draw-flag call, and the
//     device argument: the fake ports return the case memory's
//     [[this+0x8AC]+0xFF0], [this+0x8AC] and [this+0x280] as constants, so the
//     reads behind them are checked by the native engine-port section, not
//     here;
//   - the null record (slot 15 returning 0): the original faults at
//     0x00767412 reading address 0x30 (or inside slot 14, unverified), so it
//     cannot run to a result; the refusal is covered by the flows below;
//   - what the callees do: they are stubs.
//
// No x87 instruction is in the body (asm.s), so no rounding is modelled or
// tested here.

#include "Check.h"
#include "engine/render/NiDX9Renderer_DrawPrimitive.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <vector>

namespace dp = obng::engine::render::draw_primitive;
using dp::BufferRecord;
using dp::DrawInput;
using dp::EyePlan;
using dp::Handle;
using dp::IndexedDraw;
using dp::Outcome;
using dp::PlainDraw;
using dp::RendererStates;
using dp::ShaderCall;

namespace {

// ---- A recorder of the logic's port calls ----------------------------------------

enum class Kind : std::uint8_t { Slot, SubObjectSlot1, SetDrawFlag, Indexed, Plain, Switch };

struct Event {
	Kind kind = Kind::Slot;
	Handle target = 0;  // the shader for Slot
	ShaderCall call{};
	IndexedDraw indexed{};
	PlainDraw plain{};
	std::uint32_t value = 0;  // SetDrawFlag's argument, Switch's eye

	bool operator==(const Event& o) const {
		return kind == o.kind && target == o.target && call == o.call && indexed == o.indexed && plain == o.plain &&
			value == o.value;
	}
};

Event SlotEvent(Handle shader, ShaderCall call) {
	return Event{Kind::Slot, shader, call, {}, {}, 0};
}
Event SubObjectEvent() {
	return Event{Kind::SubObjectSlot1, 0, {}, {}, {}, 0};
}
Event FlagEvent(std::uint32_t value) {
	return Event{Kind::SetDrawFlag, 0, {}, {}, {}, value};
}
Event IndexedEvent(IndexedDraw d) {
	return Event{Kind::Indexed, 0, {}, d, {}, 0};
}
Event PlainEvent(PlainDraw d) {
	return Event{Kind::Plain, 0, {}, {}, d, 0};
}
Event SwitchEvent(std::uint32_t eye) {
	return Event{Kind::Switch, 0, {}, {}, {}, eye};
}

// A world of plain values behind every port; every port call is logged.
struct FakeEngine {
	std::map<Handle, Handle> candidates;  // geometry -> [geometry+0xBC]
	std::map<Handle, Handle> casts;       // object -> 0x00560920's result (0 when absent)
	Handle defaultShader = 0xD0000000u;
	RendererStates states{0x0C000000u, 0x10000000u};
	std::map<Handle, BufferRecord> records;
	std::map<Handle, std::vector<std::uint16_t>> lengths;
	std::uint32_t slot10 = 0;
	std::uint32_t slot18 = 1;
	std::vector<std::uint32_t> slot19{0};  // per call, the last repeats
	std::vector<Handle> slot15;            // per call, the last repeats
	std::function<void(FakeEngine&, const ShaderCall&)> afterSlot;

	std::vector<Event> log;
	int candidateReads = 0;
	int castCalls = 0;
	int defaultReads = 0;
	int recordReads = 0;
	std::size_t slot19Calls = 0;
	std::size_t slot15Calls = 0;

	Handle GeometryShaderCandidate(Handle geometry) {
		++candidateReads;
		return candidates[geometry];
	}
	Handle CastToShaderInterface(Handle object) {
		++castCalls;
		auto it = casts.find(object);
		return it == casts.end() ? 0 : it->second;
	}
	Handle DefaultShader() {
		++defaultReads;
		return defaultShader;
	}
	RendererStates States() const {
		return states;
	}
	BufferRecord ReadRecord(Handle record) {
		++recordReads;
		auto it = records.find(record);
		return it == records.end() ? BufferRecord{} : it->second;
	}
	std::uint16_t ReadStripLength(Handle array, std::uint32_t index) {
		auto it = lengths.find(array);
		return it == lengths.end() || index >= it->second.size() ? 0xBADu : it->second[index];
	}
	static std::uint32_t PerCall(const std::vector<std::uint32_t>& values, std::size_t& calls) {
		const std::uint32_t v = values.empty() ? 0 : values[calls < values.size() ? calls : values.size() - 1];
		++calls;
		return v;
	}
	std::uint32_t Slot(Handle shader, const ShaderCall& call) {
		log.push_back(SlotEvent(shader, call));
		std::uint32_t result = 0x5107u;  // ignored by the logic for slots 11..14, 16, 17
		switch (call.slot) {
		case 10: result = slot10; break;
		case 15: result = PerCall(slot15, slot15Calls); break;
		case 18: result = slot18; break;
		case 19: result = PerCall(slot19, slot19Calls); break;
		default: break;
		}
		if (afterSlot) {
			afterSlot(*this, call);
		}
		return result;
	}
	void SubObjectSlot1() {
		log.push_back(SubObjectEvent());
	}
	void SetDrawFlag(std::uint32_t value) {
		log.push_back(FlagEvent(value));
	}
	void DrawIndexedPrimitive(const IndexedDraw& d) {
		log.push_back(IndexedEvent(d));
	}
	void DrawPrimitive(const PlainDraw& d) {
		log.push_back(PlainEvent(d));
	}
	void Switch(std::uint8_t eye) {
		log.push_back(SwitchEvent(eye));
	}

	Outcome Run(const DrawInput& in, std::uint8_t eyes = 1) {
		return dp::Draw(in, EyePlan{eyes}, *this, *this, *this, *this, *this);
	}
};

// The inputs most flows share.
constexpr Handle kGeometry = 0x0A000000u;
constexpr Handle kCandidate = 0x0A0000BCu;
constexpr Handle kShader = 0x5A000000u;
constexpr Handle kRecord0 = 0x0E000000u;  // a6
constexpr Handle kRecord1 = 0x0E000100u;  // slot 15's results
constexpr Handle kRecord2 = 0x0E000200u;
constexpr Handle kLengths = 0x0F000000u;
constexpr DrawInput kInput{kGeometry, 0x0D000000u, 0x05000000u, 0x0B000000u, 0x0C000000u, kRecord0};

FakeEngine EngineWithShader() {
	FakeEngine e;
	e.candidates[kGeometry] = kCandidate;
	e.casts[kCandidate] = kShader;
	e.records[kRecord0] = BufferRecord{0x30, 0x7000, 0x10, 4, 0x20, 2, 0};
	e.slot15 = {kRecord1};
	return e;
}

// The original's per-pass sequence as the map reads it, for a shader and a record chain.
std::vector<Event> PassPrefix(Handle shader, const DrawInput& in, Handle previous, Handle record, RendererStates s) {
	return {SlotEvent(shader, dp::SevenArgumentCall(12, in, previous, s)),
		SlotEvent(shader, dp::EightArgumentCall(13, in, previous, s)),
		SlotEvent(shader, dp::RecordCall(in, previous, s)),
		SlotEvent(shader, dp::EightArgumentCall(14, in, record, s)),
		SubObjectEvent()};
}
std::vector<Event> PassSuffix(Handle shader, const DrawInput& in, Handle record, RendererStates s) {
	return {SlotEvent(shader, dp::EightArgumentCall(16, in, record, s)), SlotEvent(shader, dp::NoArgumentCall(19))};
}
std::vector<Event> Open(Handle shader, const DrawInput& in, RendererStates s) {
	return {SlotEvent(shader, dp::SevenArgumentCall(10, in, in.record, s)),
		SlotEvent(shader, dp::SevenArgumentCall(11, in, in.record, s)), SlotEvent(shader, dp::NoArgumentCall(18))};
}
std::vector<Event> Close(Handle shader, const DrawInput& in, Handle last, RendererStates s) {
	return {SlotEvent(shader, dp::SevenArgumentCall(17, in, last, s)), FlagEvent(0)};
}
void Append(std::vector<Event>& to, const std::vector<Event>& more) {
	to.insert(to.end(), more.begin(), more.end());
}

// ---- The pure decisions -----------------------------------------------------------

void PureDecisions() {
	Section("B1: the geometry's shader is looked at only for a non-null geometry (0x007672FD)");
	Check(!dp::LooksAtGeometryShader(0), "geometry 0: no cast");
	Check(dp::LooksAtGeometryShader(1) && dp::LooksAtGeometryShader(0x80000000u), "any non-zero geometry: the cast");

	Section("B2: the cast result when non-null, else the default shader, read only then (0x00767317)");
	int reads = 0;
	auto readDefault = [&reads] {
		++reads;
		return Handle{0xD0000000u};
	};
	Check(dp::ChooseShader(kShader, readDefault) == kShader && reads == 0, "cast succeeded: its shader, default not read");
	Check(dp::ChooseShader(0, readDefault) == 0xD0000000u && reads == 1, "cast failed: the default shader");
	Check(dp::ChooseShader(0, [] { return Handle{0}; }) == 0, "the default is not null-tested: 0 is used as is");

	Section("B3: slot 10 refuses on any non-zero 32-bit result (TEST EAX,EAX 0x00767345)");
	Check(!dp::PipelineRefused(0), "0 opens the pipeline");
	Check(dp::PipelineRefused(1) && dp::PipelineRefused(0x100) && dp::PipelineRefused(0x80000000u),
		"1, 0x100 (AL 0) and 0x80000000 refuse");

	Section("B4 / B9: a pass runs on any non-zero 32-bit result of slot 18 / slot 19");
	Check(!dp::PassRuns(0), "0: no (further) pass");
	Check(dp::PassRuns(1) && dp::PassRuns(0x100) && dp::PassRuns(0xFFFFFFFFu), "1, 0x100 (AL 0), 0xFFFFFFFF: a pass");

	Section("B5: indexed exactly when record+0x30 is non-zero (0x00767412)");
	BufferRecord r{};
	Check(!dp::IsIndexed(r), "index buffer 0: DrawPrimitive");
	r.indexBuffer = 0x80000000u;
	Check(dp::IsIndexed(r), "any non-zero index buffer: DrawIndexedPrimitive");

	Section("B6 / B8: DIP number i runs while i < record+0x44, unsigned (JBE 0x00767422, JC 0x0076747E)");
	r.arrayCount = 0;
	Check(!dp::DrawsIndexedEntry(r, 0), "arrayCount 0: no DIP at all (B6)");
	r.arrayCount = 3;
	Check(dp::DrawsIndexedEntry(r, 0) && dp::DrawsIndexedEntry(r, 2) && !dp::DrawsIndexedEntry(r, 3),
		"arrayCount 3: entries 0..2, not 3 (B8)");
	r.arrayCount = 0x80000000u;
	Check(dp::DrawsIndexedEntry(r, 0x7FFFFFFFu), "0x80000000 is a large count, not a negative one (unsigned)");

	Section("B7: count = record+0x3C without lengths, else the u16 length minus 2 in 32 bits (0x00767433..0x0076743C)");
	BufferRecord noLengths{};
	noLengths.triangleCount = 0x40;
	Check(dp::PrimitiveCount(noLengths, 0) == 0x40 && dp::PrimitiveCount(noLengths, 9) == 0x40,
		"no length array: triangleCount, whatever length is passed");
	BufferRecord withLengths{};
	withLengths.arrayLengths = kLengths;
	withLengths.triangleCount = 0x40;
	Check(dp::PrimitiveCount(withLengths, 5) == 3, "length 5: 3");
	Check(dp::PrimitiveCount(withLengths, 2) == 0, "length 2: 0");
	Check(dp::PrimitiveCount(withLengths, 1) == 0xFFFFFFFFu, "length 1 wraps to 0xFFFFFFFF (unchecked SUB)");
	Check(dp::PrimitiveCount(withLengths, 0) == 0xFFFFFFFEu, "length 0 wraps to 0xFFFFFFFE");
	Check(dp::PrimitiveCount(withLengths, 0xFFFF) == 0xFFFDu, "length 0xFFFF: zero-extended, 0xFFFD");

	Section("B8: start += count + 2, 32-bit (LEA ECX,[EAX+EBP+0x2] 0x00767468)");
	Check(dp::NextStartIndex(0, 3) == 5, "0 + 3 + 2 = 5");
	Check(dp::NextStartIndex(5, 0xFFFFFFFFu) == 6, "a wrapped count of 0xFFFFFFFF advances by 1");
	Check(dp::NextStartIndex(6, 0xFFFFFFFEu) == 6, "a wrapped count of 0xFFFFFFFE does not advance");
	Check(dp::NextStartIndex(0xFFFFFFF0u, 0x20) == 0x12, "the start itself wraps");

	Section("The argument lists, first argument first, as pushed (map, \"Every indirect call\")");
	const RendererStates s{0x111u, 0x222u};
	const ShaderCall seven = dp::SevenArgumentCall(10, kInput, kRecord0, s);
	Check(seven.slot == 10 && seven.argumentCount == 7 && seven.arguments[0] == kInput.geometry &&
			  seven.arguments[1] == kInput.skin && seven.arguments[2] == kRecord0 && seven.arguments[3] == 0x111u &&
			  seven.arguments[4] == 0x222u && seven.arguments[5] == kInput.world && seven.arguments[6] == kInput.worldBound,
		"slots 10/11/12/17: (a1, a3, ESI, this+0xC, this+0x10, a4, a5)");
	const ShaderCall eight = dp::EightArgumentCall(13, kInput, kRecord1, s);
	Check(eight.slot == 13 && eight.argumentCount == 8 && eight.arguments[0] == kInput.geometry &&
			  eight.arguments[1] == kInput.skin && eight.arguments[2] == 0 && eight.arguments[3] == kRecord1 &&
			  eight.arguments[4] == 0x111u && eight.arguments[5] == 0x222u && eight.arguments[6] == kInput.world &&
			  eight.arguments[7] == kInput.worldBound,
		"slots 13/14/16: (a1, a3, 0, ESI, this+0xC, this+0x10, a4, a5)");
	const ShaderCall record = dp::RecordCall(kInput, kRecord1, s);
	Check(record.slot == 15 && record.argumentCount == 4 && record.arguments[0] == kInput.geometry &&
			  record.arguments[1] == 0 && record.arguments[2] == kRecord1 && record.arguments[3] == 0x111u,
		"slot 15: (a1, 0, ESI, this+0xC) - no effect state");
	Check(dp::NoArgumentCall(18).argumentCount == 0 && dp::NoArgumentCall(19).slot == 19, "slots 18/19: no argument");
	Check(!(dp::SevenArgumentCall(10, kInput, kRecord0, s) == dp::SevenArgumentCall(11, kInput, kRecord0, s)),
		"calls of two slots differ");

	Section("The draw arguments (0x00767451..0x00767461 and 0x00767497..0x007674A0)");
	const BufferRecord full{0x50, 0x7100, 0xFFFFFFF0u, 5, 0x99, 4, kLengths};
	Check(dp::MakeIndexedDraw(full, 7, 3) == IndexedDraw{5, 0xFFFFFFF0u, 0, 0x50, 7, 3},
		"DIP: (+0x38, +0x34 as its bit pattern, 0, +0x14, start, count)");
	Check(dp::MakePlainDraw(full) == PlainDraw{5, 0xFFFFFFF0u, 0x99}, "DP: (+0x38, +0x34, +0x3C)");
}

// ---- The draw, one eye: the original's sequence --------------------------------------

void OneEyeFlows() {
	const RendererStates s{0x0C000000u, 0x10000000u};

	Section("B1 + B4: null geometry draws with the default shader; slot 18 0 runs no pass, slot 17 gets a6");
	{
		FakeEngine e;
		e.slot18 = 0;
		DrawInput in{0, 0, 0, 0x0B000000u, 0x0C000000u, kRecord0};
		const Outcome out = e.Run(in);
		std::vector<Event> want = Open(e.defaultShader, in, s);
		Append(want, Close(e.defaultShader, in, kRecord0, s));
		Check(out == Outcome::Drawn, "the pipeline closed");
		Check(e.candidateReads == 0 && e.castCalls == 0 && e.defaultReads == 1, "no +0xBC read, no cast, the default once");
		Check(e.log == want, "slots 10, 11, 18, 17 (with a6), then the draw flag 0");
		Check(e.recordReads == 0, "no record is read without a pass");
	}

	Section("B2: a failed cast falls back to the default shader; a successful one never reads it");
	{
		FakeEngine e = EngineWithShader();
		e.casts.clear();
		e.slot18 = 0;
		e.Run(kInput);
		Check(e.candidateReads == 1 && e.castCalls == 1 && e.defaultReads == 1, "cast 0: the default is read");
		Check(!e.log.empty() && e.log.front().target == e.defaultShader, "every slot call goes to the default shader");
		FakeEngine f = EngineWithShader();
		f.slot18 = 0;
		f.Run(kInput);
		Check(f.castCalls == 1 && f.defaultReads == 0, "cast succeeded: the default is never read");
		bool all = !f.log.empty();
		for (const Event& ev : f.log) {
			all = all && (ev.kind != Kind::Slot || ev.target == kShader);
		}
		Check(all, "every slot call goes to the cast shader");
	}

	Section("B3: slot 10 non-zero returns at once - no slot 11, no pass, no close");
	{
		FakeEngine e = EngineWithShader();
		e.slot10 = 0x100;
		const Outcome out = e.Run(kInput);
		Check(out == Outcome::PipelineRefused, "PipelineRefused");
		Check(e.log.size() == 1 && e.log[0] == SlotEvent(kShader, dp::SevenArgumentCall(10, kInput, kRecord0, s)),
			"only slot 10 ran");
	}

	Section("B5: no index buffer - one DrawPrimitive, arrayCount and lengths ignored");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x24, 0, 0x8, 5, 0x22, 3, kLengths};
		const Outcome out = e.Run(kInput);
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, kRecord1, s));
		want.push_back(PlainEvent(PlainDraw{5, 0x8, 0x22}));
		Append(want, PassSuffix(kShader, kInput, kRecord1, s));
		Append(want, Close(kShader, kInput, kRecord1, s));
		Check(out == Outcome::Drawn, "the pipeline closed");
		Check(e.log == want, "open, one pass with one DrawPrimitive(+0x38, +0x34, +0x3C), close");
	}

	Section("B6: index buffer with arrayCount 0 - no draw, slot 16 still runs");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x24, 0x7100, 0x8, 4, 0x22, 0, kLengths};
		e.Run(kInput);
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, kRecord1, s));
		Append(want, PassSuffix(kShader, kInput, kRecord1, s));
		Append(want, Close(kShader, kInput, kRecord1, s));
		Check(e.log == want, "the slot-1 call is followed directly by slot 16");
	}

	Section("B7 without lengths, B8 not taken: one DIP with triangleCount and start 0");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x40, 0x7100, 0x18, 4, 0x40, 1, 0};
		e.Run(kInput);
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, kRecord1, s));
		want.push_back(IndexedEvent(IndexedDraw{4, 0x18, 0, 0x40, 0, 0x40}));
		Append(want, PassSuffix(kShader, kInput, kRecord1, s));
		Append(want, Close(kShader, kInput, kRecord1, s));
		Check(e.log == want, "DIP(+0x38, +0x34, 0, +0x14, 0, +0x3C)");
	}

	Section("B7 without lengths, B8 taken: every DIP gets triangleCount, start steps by it + 2");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x40, 0x7100, 0x18, 4, 0x40, 3, 0};
		e.Run(kInput);
		std::vector<Event> draws;
		for (const Event& ev : e.log) {
			if (ev.kind == Kind::Indexed) {
				draws.push_back(ev);
			}
		}
		Check(draws.size() == 3 && draws[0].indexed.startIndex == 0 && draws[1].indexed.startIndex == 0x42 &&
				  draws[2].indexed.startIndex == 0x84 && draws[2].indexed.primitiveCount == 0x40,
			"three DIPs at 0, 0x42, 0x84, each 0x40 primitives");
	}

	Section("B7 with lengths, B8 taken: lengths 5, 1, 0, 7 - the counts and starts wrap as the original's");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x50, 0x7100, 0x20, 5, 0x99, 4, kLengths};
		e.lengths[kLengths] = {5, 1, 0, 7};
		e.Run(kInput);
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, kRecord1, s));
		want.push_back(IndexedEvent(IndexedDraw{5, 0x20, 0, 0x50, 0, 3}));
		want.push_back(IndexedEvent(IndexedDraw{5, 0x20, 0, 0x50, 5, 0xFFFFFFFFu}));
		want.push_back(IndexedEvent(IndexedDraw{5, 0x20, 0, 0x50, 6, 0xFFFFFFFEu}));
		want.push_back(IndexedEvent(IndexedDraw{5, 0x20, 0, 0x50, 6, 5}));
		Append(want, PassSuffix(kShader, kInput, kRecord1, s));
		Append(want, Close(kShader, kInput, kRecord1, s));
		Check(e.log == want, "starts 0, 5, 6, 6; counts 3, 0xFFFFFFFF, 0xFFFFFFFE, 5");
	}

	Section("B9: two passes - pass 2's slots 12/13/15 get pass 1's record, slot 17 the last");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x40, 0x7100, 0x18, 4, 0x40, 1, 0};
		e.records[kRecord2] = BufferRecord{0x28, 0, 0x4, 5, 0x12, 0, 0};
		e.slot15 = {kRecord1, kRecord2};
		e.slot19 = {0x100, 0};
		e.Run(kInput);
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, kRecord1, s));
		want.push_back(IndexedEvent(IndexedDraw{4, 0x18, 0, 0x40, 0, 0x40}));
		Append(want, PassSuffix(kShader, kInput, kRecord1, s));
		Append(want, PassPrefix(kShader, kInput, kRecord1, kRecord2, s));
		want.push_back(PlainEvent(PlainDraw{5, 0x4, 0x12}));
		Append(want, PassSuffix(kShader, kInput, kRecord2, s));
		Append(want, Close(kShader, kInput, kRecord2, s));
		Check(e.log == want, "the record chain of ESI (C8), one record read per pass");
		Check(e.recordReads == 2, "two record reads");
	}

	Section("Refusal: slot 15 returns 0 - slot 14 and the slot-1 call as the original, then stop where it faults");
	{
		FakeEngine e = EngineWithShader();
		e.slot15 = {0};
		const Outcome out = e.Run(kInput);
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, 0, s));
		Check(out == Outcome::NullRecord, "NullRecord");
		Check(e.log == want, "slot 14 got record 0, the slot-1 call ran; no draw, no slot 16/19/17, no draw flag");
		Check(e.recordReads == 0, "the null record is never read");
	}

	Section("Refusal in a later pass: the first pass completes, the second stops at its null record");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x24, 0, 0x8, 5, 0x22, 0, 0};
		e.slot15 = {kRecord1, 0};
		e.slot19 = {1, 0};
		const Outcome out = e.Run(kInput);
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, kRecord1, s));
		want.push_back(PlainEvent(PlainDraw{5, 0x8, 0x22}));
		Append(want, PassSuffix(kShader, kInput, kRecord1, s));
		Append(want, PassPrefix(kShader, kInput, kRecord1, 0, s));
		Check(out == Outcome::NullRecord && e.log == want, "pass 1 drawn, pass 2 stops after its slot-1 call");
	}

	Section("this+0x0C / this+0x10 are re-read before every call, as the original's loads");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x24, 0, 0x8, 5, 0x22, 0, 0};
		e.afterSlot = [](FakeEngine& self, const ShaderCall& call) {
			if (call.slot == 11) {
				self.states = RendererStates{0xAAAA0000u, 0xBBBB0000u};
			}
		};
		e.Run(kInput);
		bool before = e.log[1].call.arguments[3] == 0x0C000000u;  // slot 11 itself
		bool after = e.log[3].call.slot == 12 && e.log[3].call.arguments[3] == 0xAAAA0000u &&
					 e.log[3].call.arguments[4] == 0xBBBB0000u;
		Check(before && after, "slot 11 sees the old states, slot 12 the states slot 11 left");
	}
}

// ---- The draw, two eyes: the contract of StereoDraw.h --------------------------------

void TwoEyeFlows() {
	const RendererStates s{0x0C000000u, 0x10000000u};

	Section("Eye plans other than 2 draw once and never switch (DrawsTwice is eyes == 2)");
	for (std::uint8_t eyes : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{3}}) {
		FakeEngine one = EngineWithShader();
		one.records[kRecord1] = BufferRecord{0x40, 0x7100, 0x18, 4, 0x40, 2, 0};
		FakeEngine other = one;
		one.Run(kInput, 1);
		other.Run(kInput, eyes);
		bool noSwitch = true;
		for (const Event& ev : other.log) {
			noSwitch = noSwitch && ev.kind != Kind::Switch;
		}
		std::printf("    eyes %u\n", static_cast<unsigned>(eyes));
		Check(noSwitch && other.log == one.log, "the one-eye sequence, no Switch");
	}

	Section("Two eyes, indexed with several DIPs: the whole DIP loop again between Switch(1) and Switch(0)");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x50, 0x7100, 0x20, 5, 0x99, 3, kLengths};
		e.lengths[kLengths] = {5, 4, 6};
		const Outcome out = e.Run(kInput, 2);
		const std::vector<Event> draws{IndexedEvent(IndexedDraw{5, 0x20, 0, 0x50, 0, 3}),
			IndexedEvent(IndexedDraw{5, 0x20, 0, 0x50, 5, 2}), IndexedEvent(IndexedDraw{5, 0x20, 0, 0x50, 9, 4})};
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, kRecord1, s));
		Append(want, draws);
		want.push_back(SwitchEvent(1));
		Append(want, draws);
		want.push_back(SwitchEvent(0));
		Append(want, PassSuffix(kShader, kInput, kRecord1, s));
		Append(want, Close(kShader, kInput, kRecord1, s));
		Check(out == Outcome::Drawn, "the pipeline closed");
		Check(e.log == want, "slots 12..14 and the record once; the draws twice with the start reset; then slot 16");
		Check(e.recordReads == 1, "the record is read once per pass");
	}

	Section("Two eyes, no index buffer: DrawPrimitive twice");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x24, 0, 0x8, 5, 0x22, 0, 0};
		e.Run(kInput, 2);
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, kRecord1, s));
		want.push_back(PlainEvent(PlainDraw{5, 0x8, 0x22}));
		want.push_back(SwitchEvent(1));
		want.push_back(PlainEvent(PlainDraw{5, 0x8, 0x22}));
		want.push_back(SwitchEvent(0));
		Append(want, PassSuffix(kShader, kInput, kRecord1, s));
		Append(want, Close(kShader, kInput, kRecord1, s));
		Check(e.log == want, "DP, Switch(1), DP, Switch(0), slot 16");
	}

	Section("Two eyes, arrayCount 0: the switches run with nothing between (the contract read literally)");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x24, 0x7100, 0x8, 4, 0x22, 0, 0};
		e.Run(kInput, 2);
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, kRecord1, s));
		want.push_back(SwitchEvent(1));
		want.push_back(SwitchEvent(0));
		Append(want, PassSuffix(kShader, kInput, kRecord1, s));
		Append(want, Close(kShader, kInput, kRecord1, s));
		Check(e.log == want, "slot-1 call, Switch(1), Switch(0), slot 16");
	}

	Section("Two eyes, two passes: one switch pair per pass");
	{
		FakeEngine e = EngineWithShader();
		e.records[kRecord1] = BufferRecord{0x40, 0x7100, 0x18, 4, 0x40, 1, 0};
		e.records[kRecord2] = BufferRecord{0x28, 0, 0x4, 5, 0x12, 0, 0};
		e.slot15 = {kRecord1, kRecord2};
		e.slot19 = {1, 0};
		e.Run(kInput, 2);
		const Event dip = IndexedEvent(IndexedDraw{4, 0x18, 0, 0x40, 0, 0x40});
		const Event plain = PlainEvent(PlainDraw{5, 0x4, 0x12});
		std::vector<Event> want = Open(kShader, kInput, s);
		Append(want, PassPrefix(kShader, kInput, kRecord0, kRecord1, s));
		Append(want, {dip, SwitchEvent(1), dip, SwitchEvent(0)});
		Append(want, PassSuffix(kShader, kInput, kRecord1, s));
		Append(want, PassPrefix(kShader, kInput, kRecord1, kRecord2, s));
		Append(want, {plain, SwitchEvent(1), plain, SwitchEvent(0)});
		Append(want, PassSuffix(kShader, kInput, kRecord2, s));
		Append(want, Close(kShader, kInput, kRecord2, s));
		Check(e.log == want, "per pass: draws, Switch(1), draws, Switch(0), slot 16");
	}

	Section("Two eyes, refusals: no switch on B3, B4 or a null record");
	{
		FakeEngine refused = EngineWithShader();
		refused.slot10 = 1;
		FakeEngine noPass = EngineWithShader();
		noPass.slot18 = 0;
		FakeEngine nullRecord = EngineWithShader();
		nullRecord.slot15 = {0};
		const Outcome a = refused.Run(kInput, 2);
		const Outcome b = noPass.Run(kInput, 2);
		const Outcome c = nullRecord.Run(kInput, 2);
		bool noSwitch = true;
		for (const auto* log : {&refused.log, &noPass.log, &nullRecord.log}) {
			for (const Event& ev : *log) {
				noSwitch = noSwitch && ev.kind != Kind::Switch;
			}
		}
		Check(a == Outcome::PipelineRefused && b == Outcome::Drawn && c == Outcome::NullRecord, "the same outcomes");
		Check(noSwitch, "no Switch in any of them");
	}
}

// ---- The differential gate: the original's trace ---------------------------------------

// The synthetic memory of the cases (tools/difftest/cases/0x007672F0.json, "note").
constexpr std::uint32_t kAny = 0xFFFF0002u;  // ECX is not an input of this callee
constexpr Handle kEmuGeometry = 0x10001000u;  // this is 0x10000000 (ECX of every case)
constexpr Handle kEmuCandidate = 0x10001800u;  // [geometry+0xBC]
constexpr Handle kEmuDefaultShader = 0x10003000u;  // [this+0xA94]
constexpr Handle kEmuRenderState = 0x10004000u;    // [this+0x8AC]
constexpr Handle kEmuSubObject = 0x10005000u;      // [[this+0x8AC]+0xFF0]
constexpr Handle kEmuDevice = 0x10006000u;         // [this+0x280]
constexpr Handle kEmuLengths = 0x10008000u;
constexpr RendererStates kEmuStates{0x11000000u, 0x11000100u};  // [this+0x0C], [this+0x10]
constexpr std::uint32_t kEmuCapture = 0x10010000u;              // + 0x10 * slot; DIP + 0x200

// The stubs.
constexpr std::uint32_t kCast = 0x00560920u;
constexpr std::uint32_t SlotStub(unsigned slot) {
	return 0x20000000u + 0x100u * slot;
}
constexpr std::uint32_t kSubObjectStub = 0x20002000u;
constexpr std::uint32_t kDrawFlagStub = 0x20002100u;
constexpr std::uint32_t kDrawPrimitiveStub = 0x20002200u;
constexpr std::uint32_t kDrawIndexedStub = 0x20002300u;
constexpr std::uint32_t kSwitchMarker = 0xFFFF1000u;  // not a stub: the two-eye expectation's Switch(eye)
constexpr std::uint32_t kStubSpaceEnd = 0x20003000u;

// The return address of each call site (asm.s: the instruction after the CALL).
constexpr std::uint32_t SlotReturn(unsigned slot) {
	switch (slot) {
	case 10: return 0x00767345u;
	case 11: return 0x0076736Fu;
	case 12: return 0x007673A2u;
	case 13: return 0x007673C6u;
	case 14: return 0x007673FDu;
	case 15: return 0x007673D7u;
	case 16: return 0x007674C7u;
	case 17: return 0x007674FAu;
	case 18: return 0x00767378u;
	case 19: return 0x007674D0u;
	default: return 0;
	}
}

struct TracedCall {
	std::uint32_t callee;
	std::uint32_t from;
	std::uint32_t ecx;
	std::uint32_t args[6];
};

struct StubWrite {
	std::uint32_t at;
	std::uint32_t address;
	std::uint32_t value;
};

struct RecordAt {
	Handle address;
	BufferRecord record;
};

struct CaseInputs {
	std::uint32_t stack[6];  // a1..a6
	Handle cast;             // 0x00560920's result
	std::uint32_t slot10;
	std::uint32_t slot18;
	std::vector<std::uint32_t> slot19;  // per call, the last repeats
	std::vector<Handle> slot15;
	std::vector<RecordAt> records;
	std::vector<std::uint16_t> lengths;  // at kEmuLengths
};

struct EmulatorCase {
	const char* name;
	CaseInputs input;
	std::uint32_t eax;
	std::vector<TracedCall> calls;
	std::vector<StubWrite> writes;
};

// Transcribed from tools/difftest/cases/0x007672F0.json (inputs) and
// tools/difftest/expected/0x007672F0.json (calls, writes, EAX), 2026-11-04.
const std::vector<EmulatorCase> kCases = {
	{
		"B1 null geometry (the 0x0076B160 shape): default shader, no cast; B4 slot 18 returns 0, no pass",
		// inputs (tools/difftest/cases/0x007672F0.json)
		{{0x00000000u, 0x00000000u, 0x00000000u, 0x1000C000u, 0x1000D000u, 0x10007000u}, 0x00000000u, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u},
			{{0x10007000u, {0x00000030u, 0x00007000u, 0x00000010u, 0x00000004u, 0x00000020u, 0x00000002u, 0x00000000u}}},
			{}},
		// the original's result (tools/difftest/expected/0x007672F0.json)
		0x000000FCu,
		{
			{0x20000A00u, 0x00767345u, 0x10003000u, {0x00000000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000B00u, 0x0076736Fu, 0x10003000u, {0x00000000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20001200u, 0x00767378u, 0x10003000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20001100u, 0x007674FAu, 0x10003000u, {0x00000000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20002100u, 0x0076750Eu, 0x10004000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u}}},
		{
			{0x20000A04u, 0x100100A0u, 0x1000D000u},
			{0x20000B04u, 0x100100B0u, 0x1000D000u},
			{0x20001104u, 0x10010110u, 0x1000D000u}},
	},
	{
		"B2 cast fails: default shader; B3 slot 10 returns 0x100 (AL 0): refused at once",
		// inputs (tools/difftest/cases/0x007672F0.json)
		{{0x10001000u, 0x10009000u, 0x1000B000u, 0x1000C000u, 0x1000D000u, 0x10007000u}, 0x00000000u, 0x00000100u, 0x00000001u, {0x00000000u}, {0x10007100u},
			{{0x10007000u, {0x00000030u, 0x00007000u, 0x00000010u, 0x00000004u, 0x00000020u, 0x00000002u, 0x00000000u}}},
			{}},
		// the original's result (tools/difftest/expected/0x007672F0.json)
		0x00000100u,
		{
			{0x00560920u, 0x00767310u, 0x10000000u, {0x00B42858u, 0x10001800u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
			{0x20000A00u, 0x00767345u, 0x10003000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}}},
		{
			{0x20000A04u, 0x100100A0u, 0x1000D000u}},
	},
	{
		"B2 cast succeeds; one pass (slot 18 0x200, slot 19 0); B5 no index buffer: one DrawPrimitive, arrayCount ignored",
		// inputs (tools/difftest/cases/0x007672F0.json)
		{{0x10001000u, 0x10009000u, 0x1000B000u, 0x1000C000u, 0x1000D000u, 0x10007000u}, 0x10002000u, 0x00000000u, 0x00000200u, {0x00000000u}, {0x10007100u},
			{{0x10007000u, {0x00000030u, 0x00007000u, 0x00000010u, 0x00000004u, 0x00000020u, 0x00000002u, 0x00000000u}}, {0x10007100u, {0x00000024u, 0x00000000u, 0x00000008u, 0x00000005u, 0x00000022u, 0x00000003u, 0x10008000u}}},
			{9, 9, 9}},
		// the original's result (tools/difftest/expected/0x007672F0.json)
		0x000000FCu,
		{
			{0x00560920u, 0x00767310u, 0x10000000u, {0x00B42858u, 0x10001800u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
			{0x20000A00u, 0x00767345u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000B00u, 0x0076736Fu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20001200u, 0x00767378u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20000C00u, 0x007673A2u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000D00u, 0x007673C6u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x11000100u}},
			{0x20000F00u, 0x007673D7u, 0x10002000u, {0x10001000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x00000000u, 0x00000000u}},
			{0x20000E00u, 0x007673FDu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20002000u, 0x00767410u, 0x10005000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20002200u, 0x007674A3u, 0x00000005u, {0x10006000u, 0x00000005u, 0x00000008u, 0x00000022u, 0x00000000u, 0x00000000u}},
			{0x20001000u, 0x007674C7u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20001300u, 0x007674D0u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20001100u, 0x007674FAu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007100u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20002100u, 0x0076750Eu, 0x10004000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u}}},
		{
			{0x20000A04u, 0x100100A0u, 0x1000D000u},
			{0x20000B04u, 0x100100B0u, 0x1000D000u},
			{0x20000C04u, 0x100100C0u, 0x1000D000u},
			{0x20000D04u, 0x100100D0u, 0x1000C000u},
			{0x20000D0Du, 0x100100D4u, 0x1000D000u},
			{0x20000E04u, 0x100100E0u, 0x1000C000u},
			{0x20000E0Du, 0x100100E4u, 0x1000D000u},
			{0x20001004u, 0x10010100u, 0x1000C000u},
			{0x2000100Du, 0x10010104u, 0x1000D000u},
			{0x20001104u, 0x10010110u, 0x1000D000u}},
	},
	{
		"B6 index buffer, arrayCount 0: no draw, slot 16 still runs",
		// inputs (tools/difftest/cases/0x007672F0.json)
		{{0x10001000u, 0x10009000u, 0x1000B000u, 0x1000C000u, 0x1000D000u, 0x10007000u}, 0x10002000u, 0x00000000u, 0x00000001u, {0x00000000u}, {0x10007100u},
			{{0x10007000u, {0x00000030u, 0x00007000u, 0x00000010u, 0x00000004u, 0x00000020u, 0x00000002u, 0x00000000u}}, {0x10007100u, {0x00000024u, 0x00007100u, 0x00000008u, 0x00000004u, 0x00000022u, 0x00000000u, 0x10008000u}}},
			{9}},
		// the original's result (tools/difftest/expected/0x007672F0.json)
		0x000000FCu,
		{
			{0x00560920u, 0x00767310u, 0x10000000u, {0x00B42858u, 0x10001800u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
			{0x20000A00u, 0x00767345u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000B00u, 0x0076736Fu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20001200u, 0x00767378u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20000C00u, 0x007673A2u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000D00u, 0x007673C6u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x11000100u}},
			{0x20000F00u, 0x007673D7u, 0x10002000u, {0x10001000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x00000000u, 0x00000000u}},
			{0x20000E00u, 0x007673FDu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20002000u, 0x00767410u, 0x10005000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20001000u, 0x007674C7u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20001300u, 0x007674D0u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20001100u, 0x007674FAu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007100u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20002100u, 0x0076750Eu, 0x10004000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}}},
		{
			{0x20000A04u, 0x100100A0u, 0x1000D000u},
			{0x20000B04u, 0x100100B0u, 0x1000D000u},
			{0x20000C04u, 0x100100C0u, 0x1000D000u},
			{0x20000D04u, 0x100100D0u, 0x1000C000u},
			{0x20000D0Du, 0x100100D4u, 0x1000D000u},
			{0x20000E04u, 0x100100E0u, 0x1000C000u},
			{0x20000E0Du, 0x100100E4u, 0x1000D000u},
			{0x20001004u, 0x10010100u, 0x1000C000u},
			{0x2000100Du, 0x10010104u, 0x1000D000u},
			{0x20001104u, 0x10010110u, 0x1000D000u}},
	},
	{
		"B7 no length array, arrayCount 1: one DrawIndexedPrimitive with triangleCount; B8 not taken",
		// inputs (tools/difftest/cases/0x007672F0.json)
		{{0x10001000u, 0x10009000u, 0x1000B000u, 0x1000C000u, 0x1000D000u, 0x10007000u}, 0x10002000u, 0x00000000u, 0x00000001u, {0x00000000u}, {0x10007100u},
			{{0x10007000u, {0x00000030u, 0x00007000u, 0x00000010u, 0x00000004u, 0x00000020u, 0x00000002u, 0x00000000u}}, {0x10007100u, {0x00000040u, 0x00007100u, 0x00000018u, 0x00000004u, 0x00000040u, 0x00000001u, 0x00000000u}}},
			{}},
		// the original's result (tools/difftest/expected/0x007672F0.json)
		0x000000FCu,
		{
			{0x00560920u, 0x00767310u, 0x10000000u, {0x00B42858u, 0x10001800u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
			{0x20000A00u, 0x00767345u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000B00u, 0x0076736Fu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20001200u, 0x00767378u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20000C00u, 0x007673A2u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000D00u, 0x007673C6u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x11000100u}},
			{0x20000F00u, 0x007673D7u, 0x10002000u, {0x10001000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x00000000u, 0x00000000u}},
			{0x20000E00u, 0x007673FDu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20002000u, 0x00767410u, 0x10005000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20002300u, 0x00767464u, 0x00000004u, {0x10006000u, 0x00000004u, 0x00000018u, 0x00000000u, 0x00000040u, 0x00000000u}},
			{0x20001000u, 0x007674C7u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20001300u, 0x007674D0u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000001u, 0x7E000000u}},
			{0x20001100u, 0x007674FAu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007100u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20002100u, 0x0076750Eu, 0x10004000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000001u}}},
		{
			{0x20000A04u, 0x100100A0u, 0x1000D000u},
			{0x20000B04u, 0x100100B0u, 0x1000D000u},
			{0x20000C04u, 0x100100C0u, 0x1000D000u},
			{0x20000D04u, 0x100100D0u, 0x1000C000u},
			{0x20000D0Du, 0x100100D4u, 0x1000D000u},
			{0x20000E04u, 0x100100E0u, 0x1000C000u},
			{0x20000E0Du, 0x100100E4u, 0x1000D000u},
			{0x20002304u, 0x10010200u, 0x00000040u},
			{0x20001004u, 0x10010100u, 0x1000C000u},
			{0x2000100Du, 0x10010104u, 0x1000D000u},
			{0x20001104u, 0x10010110u, 0x1000D000u}},
	},
	{
		"B7 length array, B8 four DIPs, lengths 5, 1, 0, 7 (the u32 wrap below 2)",
		// inputs (tools/difftest/cases/0x007672F0.json)
		{{0x10001000u, 0x10009000u, 0x1000B000u, 0x1000C000u, 0x1000D000u, 0x10007000u}, 0x10002000u, 0x00000000u, 0x00000001u, {0x00000000u}, {0x10007100u},
			{{0x10007000u, {0x00000030u, 0x00007000u, 0x00000010u, 0x00000004u, 0x00000020u, 0x00000002u, 0x00000000u}}, {0x10007100u, {0x00000050u, 0x00007100u, 0x00000020u, 0x00000005u, 0x00000099u, 0x00000004u, 0x10008000u}}},
			{5, 1, 0, 7}},
		// the original's result (tools/difftest/expected/0x007672F0.json)
		0x000000FCu,
		{
			{0x00560920u, 0x00767310u, 0x10000000u, {0x00B42858u, 0x10001800u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
			{0x20000A00u, 0x00767345u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000B00u, 0x0076736Fu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20001200u, 0x00767378u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20000C00u, 0x007673A2u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000D00u, 0x007673C6u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x11000100u}},
			{0x20000F00u, 0x007673D7u, 0x10002000u, {0x10001000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x00000000u, 0x00000000u}},
			{0x20000E00u, 0x007673FDu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20002000u, 0x00767410u, 0x10005000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20002300u, 0x00767464u, 0x00000005u, {0x10006000u, 0x00000005u, 0x00000020u, 0x00000000u, 0x00000050u, 0x00000000u}},
			{0x20002300u, 0x00767464u, 0x00000005u, {0x10006000u, 0x00000005u, 0x00000020u, 0x00000000u, 0x00000050u, 0x00000005u}},
			{0x20002300u, 0x00767464u, 0x00000005u, {0x10006000u, 0x00000005u, 0x00000020u, 0x00000000u, 0x00000050u, 0x00000006u}},
			{0x20002300u, 0x00767464u, 0x00000005u, {0x10006000u, 0x00000005u, 0x00000020u, 0x00000000u, 0x00000050u, 0x00000006u}},
			{0x20001000u, 0x007674C7u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20001300u, 0x007674D0u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x7E000000u}},
			{0x20001100u, 0x007674FAu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007100u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20002100u, 0x0076750Eu, 0x10004000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000004u}}},
		{
			{0x20000A04u, 0x100100A0u, 0x1000D000u},
			{0x20000B04u, 0x100100B0u, 0x1000D000u},
			{0x20000C04u, 0x100100C0u, 0x1000D000u},
			{0x20000D04u, 0x100100D0u, 0x1000C000u},
			{0x20000D0Du, 0x100100D4u, 0x1000D000u},
			{0x20000E04u, 0x100100E0u, 0x1000C000u},
			{0x20000E0Du, 0x100100E4u, 0x1000D000u},
			{0x20002304u, 0x10010200u, 0x00000003u},
			{0x20002304u, 0x10010200u, 0xFFFFFFFFu},
			{0x20002304u, 0x10010200u, 0xFFFFFFFEu},
			{0x20002304u, 0x10010200u, 0x00000005u},
			{0x20001004u, 0x10010100u, 0x1000C000u},
			{0x2000100Du, 0x10010104u, 0x1000D000u},
			{0x20001104u, 0x10010110u, 0x1000D000u}},
	},
	{
		"B9 two passes (slot 19 0x100 then 0): pass 2 gets pass 1 record in slots 12/13/15, slot 17 gets the last",
		// inputs (tools/difftest/cases/0x007672F0.json)
		{{0x10001000u, 0x10009000u, 0x1000B000u, 0x1000C000u, 0x1000D000u, 0x10007000u}, 0x10002000u, 0x00000000u, 0x00000001u, {0x00000100u, 0x00000000u}, {0x10007100u, 0x10007200u},
			{{0x10007000u, {0x00000030u, 0x00007000u, 0x00000010u, 0x00000004u, 0x00000020u, 0x00000002u, 0x00000000u}}, {0x10007100u, {0x00000040u, 0x00007100u, 0x00000018u, 0x00000004u, 0x00000040u, 0x00000002u, 0x00000000u}}, {0x10007200u, {0x00000028u, 0x00000000u, 0x00000004u, 0x00000005u, 0x00000012u, 0x00000000u, 0x00000000u}}},
			{}},
		// the original's result (tools/difftest/expected/0x007672F0.json)
		0x000000FCu,
		{
			{0x00560920u, 0x00767310u, 0x10000000u, {0x00B42858u, 0x10001800u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
			{0x20000A00u, 0x00767345u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000B00u, 0x0076736Fu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20001200u, 0x00767378u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20000C00u, 0x007673A2u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007000u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000D00u, 0x007673C6u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x11000100u}},
			{0x20000F00u, 0x007673D7u, 0x10002000u, {0x10001000u, 0x00000000u, 0x10007000u, 0x11000000u, 0x00000000u, 0x00000000u}},
			{0x20000E00u, 0x007673FDu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20002000u, 0x00767410u, 0x10005000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x10000000u, 0x7E000000u}},
			{0x20002300u, 0x00767464u, 0x00000004u, {0x10006000u, 0x00000004u, 0x00000018u, 0x00000000u, 0x00000040u, 0x00000000u}},
			{0x20002300u, 0x00767464u, 0x00000004u, {0x10006000u, 0x00000004u, 0x00000018u, 0x00000000u, 0x00000040u, 0x00000042u}},
			{0x20001000u, 0x007674C7u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20001300u, 0x007674D0u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000002u, 0x7E000000u}},
			{0x20000C00u, 0x007673A2u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007100u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20000D00u, 0x007673C6u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x11000100u}},
			{0x20000F00u, 0x007673D7u, 0x10002000u, {0x10001000u, 0x00000000u, 0x10007100u, 0x11000000u, 0x00000000u, 0x00000000u}},
			{0x20000E00u, 0x007673FDu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007200u, 0x11000000u, 0x11000100u}},
			{0x20002000u, 0x00767410u, 0x10005000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000002u, 0x7E000000u}},
			{0x20002200u, 0x007674A3u, 0x00000005u, {0x10006000u, 0x00000005u, 0x00000004u, 0x00000012u, 0x00000000u, 0x00000000u}},
			{0x20001000u, 0x007674C7u, 0x10002000u, {0x10001000u, 0x1000B000u, 0x00000000u, 0x10007200u, 0x11000000u, 0x11000100u}},
			{0x20001300u, 0x007674D0u, 0x10002000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000002u, 0x7E000000u}},
			{0x20001100u, 0x007674FAu, 0x10002000u, {0x10001000u, 0x1000B000u, 0x10007200u, 0x11000000u, 0x11000100u, 0x1000C000u}},
			{0x20002100u, 0x0076750Eu, 0x10004000u, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000002u}}},
		{
			{0x20000A04u, 0x100100A0u, 0x1000D000u},
			{0x20000B04u, 0x100100B0u, 0x1000D000u},
			{0x20000C04u, 0x100100C0u, 0x1000D000u},
			{0x20000D04u, 0x100100D0u, 0x1000C000u},
			{0x20000D0Du, 0x100100D4u, 0x1000D000u},
			{0x20000E04u, 0x100100E0u, 0x1000C000u},
			{0x20000E0Du, 0x100100E4u, 0x1000D000u},
			{0x20002304u, 0x10010200u, 0x00000040u},
			{0x20002304u, 0x10010200u, 0x00000040u},
			{0x20001004u, 0x10010100u, 0x1000C000u},
			{0x2000100Du, 0x10010104u, 0x1000D000u},
			{0x20000C04u, 0x100100C0u, 0x1000D000u},
			{0x20000D04u, 0x100100D0u, 0x1000C000u},
			{0x20000D0Du, 0x100100D4u, 0x1000D000u},
			{0x20000E04u, 0x100100E0u, 0x1000C000u},
			{0x20000E0Du, 0x100100E4u, 0x1000D000u},
			{0x20001004u, 0x10010100u, 0x1000C000u},
			{0x2000100Du, 0x10010104u, 0x1000D000u},
			{0x20001104u, 0x10010110u, 0x1000D000u}},
	}
};

// One call with every argument the callee takes.
struct FullCall {
	std::uint32_t callee = 0;
	std::uint32_t from = 0;
	std::uint32_t ecx = 0;
	std::uint8_t argumentCount = 0;
	std::uint32_t args[8] = {};

	bool operator==(const FullCall& o) const {
		if (callee != o.callee || from != o.from || argumentCount != o.argumentCount) {
			return false;
		}
		if (ecx != kAny && o.ecx != kAny && ecx != o.ecx) {
			return false;
		}
		for (std::uint8_t i = 0; i < argumentCount; ++i) {
			if (args[i] != o.args[i]) {
				return false;
			}
		}
		return true;
	}
};

// How many dwords each stub takes, and where its capture store writes the
// dwords beyond the sixth (0 when it has none).
std::uint8_t ArgumentCount(std::uint32_t callee) {
	switch (callee) {
	case kCast: return 2;
	case kSubObjectStub: return 0;
	case kDrawFlagStub: return 1;
	case kDrawPrimitiveStub: return 4;
	case kDrawIndexedStub: return 7;
	default: break;
	}
	for (unsigned slot = 10; slot <= 19; ++slot) {
		if (callee != SlotStub(slot)) {
			continue;
		}
		switch (slot) {
		case 13:
		case 14:
		case 16: return 8;
		case 15: return 4;
		case 18:
		case 19: return 0;
		default: return 7;
		}
	}
	return 0xFF;
}
std::uint32_t CaptureAddress(std::uint32_t callee) {
	if (callee == kDrawIndexedStub) {
		return kEmuCapture + 0x200u;
	}
	return kEmuCapture + 0x10u * ((callee - 0x20000000u) / 0x100u);
}
bool IsDraw(std::uint32_t callee) {
	return callee == kDrawIndexedStub || callee == kDrawPrimitiveStub;
}

// The original's calls with the capture writes put back as arguments 7 and 8.
// ok turns false when a write does not sit where the call order puts it.
std::vector<FullCall> Merge(const EmulatorCase& c, bool& ok) {
	std::vector<FullCall> out;
	std::size_t w = 0;
	for (const TracedCall& t : c.calls) {
		FullCall f{t.callee, t.from, t.ecx, ArgumentCount(t.callee), {}};
		ok = ok && f.argumentCount != 0xFF;
		for (std::size_t i = 0; i < 6; ++i) {
			f.args[i] = t.args[i];
		}
		for (std::uint8_t i = 6; i < f.argumentCount && f.argumentCount != 0xFF; ++i) {
			ok = ok && w < c.writes.size() && c.writes[w].address == CaptureAddress(t.callee) + 4u * (i - 6u);
			f.args[i] = w < c.writes.size() ? c.writes[w].value : 0;
			++w;
		}
		if (t.callee == kCast || t.callee == kDrawIndexedStub || t.callee == kDrawPrimitiveStub) {
			f.ecx = kAny;
		}
		out.push_back(f);
	}
	ok = ok && w == c.writes.size();
	return out;
}

// The two-eye expectation from the original's one-eye trace: per pass, the
// draws between the slot-1 call and slot 16 repeated after Switch(1), then
// Switch(0), before slot 16 (StereoDraw.h).
std::vector<FullCall> WithSecondEye(const std::vector<FullCall>& original) {
	std::vector<FullCall> out;
	std::vector<FullCall> passDraws;
	bool inPass = false;
	for (const FullCall& f : original) {
		if (f.callee == kSubObjectStub) {
			inPass = true;
			passDraws.clear();
		} else if (inPass && IsDraw(f.callee)) {
			passDraws.push_back(f);
		} else if (inPass && f.callee == SlotStub(16)) {
			out.push_back(FullCall{kSwitchMarker, 0, kAny, 1, {1}});
			out.insert(out.end(), passDraws.begin(), passDraws.end());
			out.push_back(FullCall{kSwitchMarker, 0, kAny, 1, {0}});
			inPass = false;
		}
		out.push_back(f);
	}
	return out;
}

// The ports over the case's inputs, each call recorded in the emulator's terms.
struct EmulatorPorts {
	const CaseInputs& in;
	std::vector<FullCall> calls;
	std::size_t slot19Calls = 0;
	std::size_t slot15Calls = 0;

	Handle GeometryShaderCandidate(Handle geometry) const {
		return geometry == kEmuGeometry ? kEmuCandidate : 0xBADBAD00u;
	}
	Handle CastToShaderInterface(Handle object) {
		calls.push_back(FullCall{kCast, 0x00767310u, kAny, 2, {dp::engine::kShaderInterfaceRttiAddress, object}});
		return in.cast;
	}
	Handle DefaultShader() const {
		return kEmuDefaultShader;
	}
	RendererStates States() const {
		return kEmuStates;
	}
	BufferRecord ReadRecord(Handle record) const {
		for (const RecordAt& r : in.records) {
			if (r.address == record) {
				return r.record;
			}
		}
		return BufferRecord{};
	}
	std::uint16_t ReadStripLength(Handle array, std::uint32_t index) const {
		return array == kEmuLengths && index < in.lengths.size() ? in.lengths[index] : 0xBADu;
	}
	std::uint32_t Slot(Handle shader, const ShaderCall& call) {
		FullCall f{SlotStub(call.slot), SlotReturn(call.slot), shader, call.argumentCount, {}};
		for (std::uint8_t i = 0; i < call.argumentCount; ++i) {
			f.args[i] = call.arguments[i];
		}
		calls.push_back(f);
		auto perCall = [](const std::vector<std::uint32_t>& v, std::size_t& n) {
			const std::uint32_t r = v.empty() ? 0 : v[n < v.size() ? n : v.size() - 1];
			++n;
			return r;
		};
		switch (call.slot) {
		case 10: return in.slot10;
		case 15: return perCall(in.slot15, slot15Calls);
		case 18: return in.slot18;
		case 19: return perCall(in.slot19, slot19Calls);
		default: return 0x5107u;
		}
	}
	void SubObjectSlot1() {
		calls.push_back(FullCall{kSubObjectStub, 0x00767410u, kEmuSubObject, 0, {}});
	}
	void SetDrawFlag(std::uint32_t value) {
		calls.push_back(FullCall{kDrawFlagStub, 0x0076750Eu, kEmuRenderState, 1, {value}});
	}
	void DrawIndexedPrimitive(const IndexedDraw& d) {
		calls.push_back(FullCall{kDrawIndexedStub, 0x00767464u, kAny, 7,
			{kEmuDevice, d.primitiveType, d.baseVertexIndex, d.minVertexIndex, d.vertexCount, d.startIndex, d.primitiveCount}});
	}
	void DrawPrimitive(const PlainDraw& d) {
		calls.push_back(FullCall{kDrawPrimitiveStub, 0x007674A3u, kAny, 4,
			{kEmuDevice, d.primitiveType, d.startVertex, d.primitiveCount}});
	}
	void Switch(std::uint8_t eye) {
		calls.push_back(FullCall{kSwitchMarker, 0, kAny, 1, {eye}});
	}
};

void PrintFirstDifference(const std::vector<FullCall>& got, const std::vector<FullCall>& want) {
	const std::size_t n = got.size() < want.size() ? got.size() : want.size();
	for (std::size_t i = 0; i < n; ++i) {
		if (!(got[i] == want[i])) {
			std::printf("    first difference at call %zu: rebuild %08x from %08x, original %08x from %08x\n", i,
				got[i].callee, got[i].from, want[i].callee, want[i].from);
			return;
		}
	}
	if (got.size() != want.size()) {
		std::printf("    %zu calls in the rebuild, %zu in the original\n", got.size(), want.size());
	}
}

void EmulatorGate() {
	for (const EmulatorCase& c : kCases) {
		Section(c.name);
		bool merged = true;
		const std::vector<FullCall> original = Merge(c, merged);
		Check(merged, "every capture write sits where the original's call order puts it");
		bool onlyStubs = true;
		for (const StubWrite& w : c.writes) {
			onlyStubs = onlyStubs && w.at >= SlotStub(0) && w.at < kStubSpaceEnd;
		}
		Check(onlyStubs, "the body itself wrote nothing outside its stack (every write is a stub's capture)");

		const DrawInput in{c.input.stack[0], c.input.stack[1], c.input.stack[2], c.input.stack[3], c.input.stack[4],
			c.input.stack[5]};
		EmulatorPorts one{c.input, {}};
		const Outcome outcome = dp::Draw(in, EyePlan{1}, one, one, one, one, one);
		Check(one.calls == original, "one eye: the original's calls, in order, with every argument");
		if (!(one.calls == original)) {
			PrintFirstDifference(one.calls, original);
		}
		const bool closed = !c.calls.empty() && c.calls.back().callee == kDrawFlagStub;
		Check(closed ? (outcome == Outcome::Drawn && c.eax == 0xFCu)
					 : (outcome == Outcome::PipelineRefused && c.eax == c.input.slot10),
			closed ? "Drawn - the original's EAX is the draw-flag stub's 0xFC"
				   : "PipelineRefused - the original's EAX is slot 10's value");

		EmulatorPorts two{c.input, {}};
		dp::Draw(in, EyePlan{2}, two, two, two, two, two);
		const std::vector<FullCall> stereo = WithSecondEye(original);
		Check(two.calls == stereo, "two eyes: the original's calls with each pass's draws repeated between the switches");
		if (!(two.calls == stereo)) {
			PrintFirstDifference(two.calls, stereo);
		}
	}
}

// ---- The engine ports over native stand-ins ----------------------------------------
//
// NiDX9Renderer_DrawPrimitive.h's EnginePorts read the views and call through
// the vtables with the original's conventions (thiscall slots, stdcall COM
// draws, cdecl cast). Here those vtables point at native functions with the
// same conventions that log what they receive, so the offsets, the
// conventions and the argument order of the engine side run for real (the
// tests are 32-bit, tests/CMakeLists.txt). Not the game: the stand-ins only
// record.

namespace en = dp::engine;

struct NativeCall {
	int kind;  // the slot number 10..19, or one of the kinds below
	std::uint32_t self;
	std::vector<std::uint32_t> args;
	bool operator==(const NativeCall&) const = default;
};
constexpr int kNativeSubObject = 100;
constexpr int kNativeDrawFlag = 101;
constexpr int kNativeDrawPrimitive = 102;
constexpr int kNativeDrawIndexed = 103;
constexpr int kNativeCast = 104;
constexpr int kNativeSwitch = 105;

std::vector<NativeCall>& NativeLog() {
	static std::vector<NativeCall> log;
	return log;
}
std::uint32_t& NativeRecordResult() {
	static std::uint32_t record = 0;
	return record;
}
const void*& NativeCastTarget() {  // the object 0x00560920's stand-in recognises
	static const void* target = nullptr;
	return target;
}
void*& NativeCastResult() {
	static void* result = nullptr;
	return result;
}

std::uint32_t H(const void* p) {
	return en::ToHandle(p);
}

template <int N>
std::uint32_t __thiscall NativeSlot0(void* self) {
	NativeLog().push_back({N, H(self), {}});
	return N == 18 ? 1u : 0u;  // one pass
}
template <int N>
std::uint32_t __thiscall NativeSlot7(void* self, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d,
	std::uint32_t e, std::uint32_t f, std::uint32_t g) {
	NativeLog().push_back({N, H(self), {a, b, c, d, e, f, g}});
	return N == 10 ? 0u : 0x5107u;
}
template <int N>
std::uint32_t __thiscall NativeSlot8(void* self, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d,
	std::uint32_t e, std::uint32_t f, std::uint32_t g, std::uint32_t h) {
	NativeLog().push_back({N, H(self), {a, b, c, d, e, f, g, h}});
	return 0x5107u;
}
std::uint32_t __thiscall NativeSlot15(void* self, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
	NativeLog().push_back({15, H(self), {a, b, c, d}});
	return NativeRecordResult();
}
void __thiscall NativeSubObjectSlot1(void* self) {
	NativeLog().push_back({kNativeSubObject, H(self), {}});
}
void __thiscall NativeSetDrawFlag(void* self, std::uint32_t value) {
	NativeLog().push_back({kNativeDrawFlag, H(self), {value}});
}
std::int32_t __stdcall NativeDrawPrimitive(void* device, std::uint32_t type, std::uint32_t start, std::uint32_t count) {
	NativeLog().push_back({kNativeDrawPrimitive, H(device), {type, start, count}});
	return 0;
}
std::int32_t __stdcall NativeDrawIndexed(void* device, std::uint32_t type, std::int32_t base, std::uint32_t minIndex,
	std::uint32_t vertices, std::uint32_t start, std::uint32_t count) {
	NativeLog().push_back(
		{kNativeDrawIndexed, H(device), {type, static_cast<std::uint32_t>(base), minIndex, vertices, start, count}});
	return 0;
}
void* __cdecl NativeCast(const void* rtti, const void* object) {
	NativeLog().push_back({kNativeCast, 0, {H(rtti), H(object)}});
	return object == NativeCastTarget() ? NativeCastResult() : nullptr;
}

struct NativeEyeSwitch {
	void Switch(std::uint8_t eye) {
		NativeLog().push_back({kNativeSwitch, 0, {eye}});
	}
};

void NativeEnginePorts() {
	static en::ShaderInterfaceVtableView shaderVtable{{}, NativeSlot7<10>, NativeSlot7<11>, NativeSlot7<12>,
		NativeSlot8<13>, NativeSlot8<14>, NativeSlot15, NativeSlot8<16>, NativeSlot7<17>, NativeSlot0<18>,
		NativeSlot0<19>};
	static en::ShaderInterfaceView shader{&shaderVtable};
	static en::ShaderInterfaceView defaultShader{&shaderVtable};
	static en::DeviceVtableView deviceVtable{};
	deviceVtable.drawPrimitive = NativeDrawPrimitive;
	deviceVtable.drawIndexedPrimitive = NativeDrawIndexed;
	static en::DeviceView device{&deviceVtable};
	static en::RenderSubObjectVtableView subVtable{nullptr, NativeSubObjectSlot1};
	static en::RenderSubObjectView sub{&subVtable};
	static en::RenderStateVtableView stateVtable{};
	stateVtable.setDrawFlag = NativeSetDrawFlag;
	static en::RenderStateView state{};
	state.vtable = &stateVtable;
	state.subObject = &sub;
	static en::NiDX9RendererView renderer{};
	renderer.propertyState = 0x0C0C0C0Cu;
	renderer.effectState = 0x10101010u;
	renderer.device = &device;
	renderer.renderState = &state;
	renderer.defaultShader = &defaultShader;

	static int candidate = 0;
	static en::NiGeometryView geometry{};
	geometry.shader = &candidate;
	NativeCastTarget() = &candidate;
	NativeCastResult() = &shader;

	static const std::uint16_t lengths[2] = {5, 4};
	static en::GeometryBufferDataView first{};  // a6
	static en::GeometryBufferDataView indexed{};
	indexed.vertexCount = 0x50;
	indexed.indexBuffer = 0x7100;
	indexed.baseVertexIndex = 0xFFFFFFF0u;
	indexed.primitiveType = 5;
	indexed.triangleCount = 0x99;
	indexed.arrayCount = 2;
	indexed.arrayLengths = lengths;
	static en::GeometryBufferDataView plain{};
	plain.baseVertexIndex = 0x8;
	plain.primitiveType = 4;
	plain.triangleCount = 0x22;

	int data = 0;
	int skin = 0;
	int world = 0;
	int bound = 0;
	const std::uint32_t g = H(&geometry);
	const std::uint32_t k = H(&skin);
	const std::uint32_t w = H(&world);
	const std::uint32_t b = H(&bound);
	const std::uint32_t ps = 0x0C0C0C0Cu;
	const std::uint32_t es = 0x10101010u;
	const std::uint32_t a6 = H(&first);
	const std::uint32_t rec = H(&indexed);
	const std::uint32_t sh = H(&shader);
	const std::uint32_t dev = H(&device);

	Section("Engine ports, two eyes: the views' offsets and the thiscall / stdcall / cdecl calls, run natively");
	{
		NativeLog().clear();
		NativeRecordResult() = rec;
		NativeEyeSwitch eyeSwitch;
		const Outcome out = en::DrawPrimitive(&renderer, &geometry, &data, &skin, &world, &bound, &first, EyePlan{2},
			eyeSwitch, NativeCast);
		const std::vector<NativeCall> want{
			{kNativeCast, 0, {en::kShaderInterfaceRttiAddress, H(&candidate)}},
			{10, sh, {g, k, a6, ps, es, w, b}},
			{11, sh, {g, k, a6, ps, es, w, b}},
			{18, sh, {}},
			{12, sh, {g, k, a6, ps, es, w, b}},
			{13, sh, {g, k, 0, a6, ps, es, w, b}},
			{15, sh, {g, 0, a6, ps}},
			{14, sh, {g, k, 0, rec, ps, es, w, b}},
			{kNativeSubObject, H(&sub), {}},
			{kNativeDrawIndexed, dev, {5, 0xFFFFFFF0u, 0, 0x50, 0, 3}},
			{kNativeDrawIndexed, dev, {5, 0xFFFFFFF0u, 0, 0x50, 5, 2}},
			{kNativeSwitch, 0, {1}},
			{kNativeDrawIndexed, dev, {5, 0xFFFFFFF0u, 0, 0x50, 0, 3}},
			{kNativeDrawIndexed, dev, {5, 0xFFFFFFF0u, 0, 0x50, 5, 2}},
			{kNativeSwitch, 0, {0}},
			{16, sh, {g, k, 0, rec, ps, es, w, b}},
			{19, sh, {}},
			{17, sh, {g, k, rec, ps, es, w, b}},
			{kNativeDrawFlag, H(&state), {0}},
		};
		Check(out == Outcome::Drawn, "Drawn");
		Check(NativeLog() == want,
			"cast(0x00B42858, [geometry+0xBC]); slots on the cast shader with this+0x0C/+0x10; the record's fields; "
			"ECX of the slot-1 call = [[this+0x8AC]+0xFF0], of the draw flag = [this+0x8AC]; the device [this+0x280]");
	}

	Section("Engine ports, one eye, null geometry: the default shader [this+0xA94] and DrawPrimitive");
	{
		NativeLog().clear();
		NativeRecordResult() = H(&plain);
		NativeEyeSwitch eyeSwitch;
		en::DrawPrimitive(&renderer, nullptr, nullptr, nullptr, &world, &bound, &first, EyePlan{1}, eyeSwitch, NativeCast);
		const std::vector<NativeCall>& log = NativeLog();
		bool noCast = true;
		bool allDefault = true;
		for (const NativeCall& c : log) {
			noCast = noCast && c.kind != kNativeCast && c.kind != kNativeSwitch;
			allDefault = allDefault && (c.kind < 10 || c.kind > 19 || c.self == H(&defaultShader));
		}
		bool plainDraw = false;
		for (const NativeCall& c : log) {
			plainDraw = plainDraw || (c.kind == kNativeDrawPrimitive && c == NativeCall{kNativeDrawPrimitive, dev, {4, 8, 0x22}});
		}
		Check(noCast && allDefault, "no cast, no switch; every slot on the default shader");
		Check(plainDraw, "DrawPrimitive(device, +0x38, +0x34, +0x3C)");
	}
}

} // namespace

int main() {
	PureDecisions();
	OneEyeFlows();
	TwoEyeFlows();
	EmulatorGate();
	NativeEnginePorts();
	return Failures();
}
