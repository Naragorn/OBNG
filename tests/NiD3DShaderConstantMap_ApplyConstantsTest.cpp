// The flows of 0x009A8E30 (proposed NiD3DShaderConstantMap::ApplyConstants):
// the pure decisions B1..B15 of NiD3DShaderConstantMap_ApplyConstantsLogic.h,
// the whole apply over recording ports (EntryFilter::kWhole: the original's
// sequence; kView: the contract of ConstantReapply.h), the differential gate
// against the original's emulator trace, and the engine ports of
// NiD3DShaderConstantMap_ApplyConstants.h over native stand-ins with the
// original's calling conventions.
//
// The differential gate, layer 1 (docs/re-workflow.md, "the gate outside the
// workflow"). The original ran in the emulator (tools/difftest/emulate.py) on
// the synthetic inputs of tools/difftest/cases/0x009A8E30.json; its result is
// tools/difftest/expected/0x009A8E30.json (2026-11-04: all 21 cases returned
// to the sentinel with stack_delta +44 - the RET 0x2C popping the eleven
// arguments - and no emulator error). 0x006FF9C0, every dispatched map slot
// and both device methods are traced stubs, so each trace is the original's
// complete call sequence: callee, return address, ECX and six stack dwords.
// The slots with more than six arguments (+0x54, +0x5C, +0x64, +0x6C) are
// raw-code stubs that copy their arguments 7..N to a capture area; those
// copies and the body's own store [this+0x20] = a1 (0x009A8E3F) are the only
// recorded writes. Trace and writes are transcribed below, literally, one
// entry per case (measured in the emulator). The rebuild runs the same inputs
// over fake ports that hand out the same addresses as handles; each port call
// is expressed in the emulator's terms (stub, return address of the
// original's CALL, ECX, arguments) and the two sequences must be equal.
//
// What it compares, per case:
//   - every call in order: callee, return address, ECX (not for the COM
//     device calls, whose ECX is not an input of a stdcall callee: kAny),
//     and every argument the callee takes (the first six from the trace, the
//     rest from the capture writes);
//   - the body's one write: [this+0x20] = a1, size 4;
//   - EAX: the original's return value;
//   - under kView, the original's trace filtered to the slot +0x54 calls of
//     entries with a view id (derived from the original's trace and the
//     case's ids, not from the rebuild), no lookup, the same store, and the
//     result over those calls' statuses.
//
// What it cannot see:
//   - the name pointer of the lookup and the reads behind the ports: the fake
//     ports hand out the case's memory as values, so the reads of
//     [0x00B29F84], the map and entry offsets and the device's vtable are
//     checked by the native engine-port section, not here;
//   - what the callees do: they are stubs.
//
// No x87 instruction is in the body (asm.s), so no rounding is modelled or
// tested here.

#include "Check.h"
#include "engine/render/NiD3DShaderConstantMap_ApplyConstants.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <vector>

namespace ac = obng::engine::render::apply_constants;
namespace cr = obng::engine::render::constant_reapply;
using ac::ApplyInput;
using ac::ConstantRoute;
using ac::EntryFilter;
using ac::Handle;
using ac::Handler;
using ac::SlotCall;

namespace {

// ---- The pure decisions -----------------------------------------------------------

void PureDecisions() {
	Section("B1 / B15: index i is applied while i < the zero-extended u16 count, unsigned (JBE 0x009A8E60, JC 0x009A9024)");
	Check(!ac::HasEntry(0, 0), "B1: count 0 - no entry, not even index 0");
	Check(ac::HasEntry(0, 1) && !ac::HasEntry(1, 1), "count 1: index 0 only");
	Check(ac::HasEntry(0x7FFFu, 0x8000u), "count 0x8000 is zero-extended, not a negative i16");
	Check(ac::HasEntry(0xFFFEu, 0xFFFFu) && !ac::HasEntry(0xFFFFu, 0xFFFFu), "count 0xFFFF: the last index is 0xFFFE");

	Section("B2: a null entry pointer (TEST EAX,EAX / JZ 0x009A8E78)");
	Check(ac::IsNullEntry(0) && !ac::IsNullEntry(0x10004000u), "0 only");

	Section("B3: a clear byte +0x08 ends the walk (CMP byte [EAX+0x8],0 / JZ 0x009A902A)");
	Check(ac::EndsWalk(0), "0 ends it");
	Check(!ac::EndsWalk(1) && !ac::EndsWalk(0x80) && !ac::EndsWalk(0xFF), "1, 0x80, 0xFF do not");

	Section("B4 / B5: register != -1 passes without reading +0x24; -1 passes on a non-zero gate (0x009A8E92, 0x009A8E98)");
	{
		int gateReads = 0;
		auto gate = [&](std::uint32_t v) {
			return [&gateReads, v] {
				++gateReads;
				return v;
			};
		};
		Check(ac::RegisterGatePasses(5, gate(0)) && gateReads == 0, "B4: register 5 passes, the gate is not read");
		Check(ac::RegisterGatePasses(0xFFFFFFFEu, gate(0)) && gateReads == 0, "B4: 0xFFFFFFFE is not -1");
		Check(!ac::RegisterGatePasses(0xFFFFFFFFu, gate(0)) && gateReads == 1, "B5: -1 with gate 0 is skipped");
		Check(ac::RegisterGatePasses(0xFFFFFFFFu, gate(1)) && gateReads == 2, "B5 not taken: -1 with gate 1 passes");
		Check(ac::RegisterGatePasses(0xFFFFFFFFu, gate(0x80000000u)), "-1 with gate 0x80000000 passes (whole dword)");
	}

	Section("B6, B7, B10..B13: the handler by the type nibble, the low 28 bits ignored (AND ECX,0xF0000000 0x009A8EA1)");
	{
		const Handler want[16] = {Handler::kUnknown, Handler::kConstant, Handler::kDefined, Handler::kAttribute,
			Handler::kGlobal, Handler::kOperator, Handler::kObject, Handler::kUnknown, Handler::kUnknown,
			Handler::kUnknown, Handler::kUnknown, Handler::kUnknown, Handler::kUnknown, Handler::kUnknown,
			Handler::kUnknown, Handler::kUnknown};
		bool all = true;
		bool lowBits = true;
		for (std::uint32_t nibble = 0; nibble < 16; ++nibble) {
			all = all && ac::HandlerOf(nibble << 28) == want[nibble];
			lowBits = lowBits && ac::HandlerOf((nibble << 28) | 0x0ABCDEF5u) == want[nibble];
		}
		Check(all, "1 constant, 2 defined, 3 attribute, 4 global, 5 operator, 6 object, 0 and 7..F unknown");
		Check(lowBits, "the same with the low 28 bits set");
	}

	Section("B8 / B9: the route by the whole target dword (CMP ECX,1 0x009A8EC8; CMP ECX,2 0x009A8EFE)");
	Check(ac::RouteOf(1) == ConstantRoute::kVertexDevice, "1: SetVertexShaderConstantF");
	Check(ac::RouteOf(2) == ConstantRoute::kPixelDevice, "2: SetPixelShaderConstantF");
	Check(ac::RouteOf(0) == ConstantRoute::kProgram && ac::RouteOf(3) == ConstantRoute::kProgram &&
			  ac::RouteOf(0x101u) == ConstantRoute::kProgram && ac::RouteOf(0xFFFFFFFFu) == ConstantRoute::kProgram,
		"0, 3, 0x101, 0xFFFFFFFF: slot +0x58");

	Section("Device status: SETL after TEST EAX,EAX - 1 exactly for a negative HRESULT (0x009A8EF4, 0x009A8F2A)");
	Check(ac::DeviceStatus(0) == 0 && ac::DeviceStatus(1) == 0 && ac::DeviceStatus(0x7FFFFFFF) == 0,
		"S_OK, 1, 0x7FFFFFFF: 0");
	Check(ac::DeviceStatus(-1) == 1 && ac::DeviceStatus(static_cast<std::int32_t>(0x8876086Cu)) == 1 &&
			  ac::DeviceStatus(static_cast<std::int32_t>(0x80000000u)) == 1,
		"-1, D3DERR_INVALIDCALL 0x8876086C, 0x80000000: 1");

	Section("B14: the whole 32-bit status clears the flag (TEST EAX,EAX / JZ 0x009A900C)");
	Check(!ac::ClearsFlag(0), "0 keeps it");
	Check(ac::ClearsFlag(1) && ac::ClearsFlag(0x100u) && ac::ClearsFlag(0x80000000u), "1, 0x100 (AL 0), 0x80000000 clear it");

	Section("The return: (flag == 0) in the full EAX (XOR EAX,EAX; CMP; SETZ AL 0x009A902C..0x009A9033)");
	Check(ac::Result(true) == 0 && ac::Result(false) == 1, "flag set 0, cleared 1");

	Section("The filter: kWhole passes everything without reading the id; kView only defined view ids (ConstantReapply.h)");
	{
		int idReads = 0;
		auto id = [&](std::uint32_t v) {
			return [&idReads, v] {
				++idReads;
				return v;
			};
		};
		Check(ac::FilterPasses(EntryFilter::kWhole, 0x90000000u, id(1)) &&
				  ac::FilterPasses(EntryFilter::kWhole, 0x20000000u, id(1)) && idReads == 0,
			"kWhole: an unknown and a non-view defined entry pass, the id is never read");
		Check(!ac::FilterPasses(EntryFilter::kView, 0x10000000u, id(19)) &&
				  !ac::FilterPasses(EntryFilter::kView, 0x30000000u, id(19)) &&
				  !ac::FilterPasses(EntryFilter::kView, 0x60000000u, id(19)) && idReads == 0,
			"kView: constant, attribute, object entries are refused without reading the id");
		Check(ac::FilterPasses(EntryFilter::kView, 0x20000000u, id(19)) && idReads == 1, "kView: defined id 19 passes");
		Check(ac::FilterPasses(EntryFilter::kView, 0x2FFFFFFFu, id(32)), "kView: the low bits of the type are ignored");
		Check(!ac::FilterPasses(EntryFilter::kView, 0x20000000u, id(1)) &&
				  !ac::FilterPasses(EntryFilter::kView, 0x20000000u, id(33)),
			"kView: defined ids 1 and 33 are refused");
	}

	Section("The argument lists, first argument first, as pushed (0x009A8F36.., 0x009A8F4D.., 0x009A8FAA.., 0x009A8FDF..)");
	{
		const ApplyInput in{0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB};
		const SlotCall defined = ac::ElevenArgumentCall(ac::kSlotDefined, in, 0xE0);
		const SlotCall object = ac::ElevenArgumentCall(ac::kSlotObject, in, 0xE0);
		const SlotCall constant = ac::ThreeArgumentCall(ac::kSlotConstant, in, 0xE0);
		const SlotCall global = ac::ThreeArgumentCall(ac::kSlotGlobal, in, 0xE0);
		const SlotCall attribute = ac::AttributeCall(in, 0xE0, 0xEE);
		const SlotCall op = ac::OperatorCall(in, 0xE0);
		Check(defined == SlotCall{0x54, 11, {0xA1, 0xE0, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA}},
			"+0x54: (a1, entry, a2..a10)");
		Check(object == SlotCall{0x6C, 11, {0xA1, 0xE0, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA}},
			"+0x6C: the same list (C12)");
		Check(constant == SlotCall{0x58, 3, {0xA1, 0xE0, 0xAA}}, "+0x58: (a1, entry, a10)");
		Check(global == SlotCall{0x60, 3, {0xA1, 0xE0, 0xAA}}, "+0x60: (a1, entry, a10)");
		Check(attribute ==
				  SlotCall{0x5C, 13, {0xA1, 0xE0, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xEE}},
			"+0x5C: (a1, entry, a2..a11, extra data)");
		Check(op == SlotCall{0x64, 9, {0xA1, 0xE0, 0xA2, 0xA3, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA}},
			"+0x64: (a1, entry, a2, a3, a6, a7, a8, a9, a10)");
		Check(!(defined == object) && !(constant == global), "the slot offset is part of the call");
	}
}

// ---- A recorder of the logic's port calls -------------------------------------------

enum class Kind : std::uint8_t { Store, Lookup, Slot, Vertex, Pixel };

struct Event {
	Kind kind = Kind::Slot;
	Handle target = 0;  // the map (Store, Slot), the geometry (Lookup), the device (Vertex, Pixel)
	SlotCall call{};
	std::uint32_t values[3] = {};  // Store: program; Vertex / Pixel: start, data, count

	bool operator==(const Event& o) const {
		return kind == o.kind && target == o.target && call == o.call && values[0] == o.values[0] &&
			values[1] == o.values[1] && values[2] == o.values[2];
	}
};

Event StoreEvent(Handle map, Handle program) {
	return Event{Kind::Store, map, {}, {program, 0, 0}};
}
Event LookupEvent(Handle geometry) {
	return Event{Kind::Lookup, geometry, {}, {}};
}
Event SlotEvent(Handle map, SlotCall call) {
	return Event{Kind::Slot, map, call, {}};
}
Event UploadEvent(Kind kind, Handle device, std::uint32_t start, Handle data, std::uint32_t count) {
	return Event{kind, device, {}, {start, data, count}};
}

struct FakeEntry {
	std::uint8_t live = 1;
	std::uint32_t id = 0;
	std::uint32_t typeFlags = 0;
	std::uint32_t target = 0;
	std::uint32_t startRegister = 0;
	std::uint32_t registerCount = 0;
	std::uint32_t gate = 0;
	Handle data = 0;
};

constexpr Handle kMap = 0x0A000000u;
constexpr Handle kExtra = 0x0E000000u;
constexpr Handle kDevice = 0x0D000000u;

Handle EntryHandle(std::size_t i) {
	return 0x0B000000u + 0x100u * static_cast<Handle>(i);
}

FakeEntry Defined(std::uint32_t id, std::uint32_t reg = 0) {
	return FakeEntry{1, id, cr::kTypeDefined, 0, reg, 0, 0, 0};
}
FakeEntry Constant(std::uint32_t target, std::uint32_t reg, std::uint32_t count, Handle data) {
	return FakeEntry{1, 0, ac::kTypeConstant, target, reg, count, 0, data};
}
FakeEntry OfType(std::uint32_t typeFlags, std::uint32_t reg = 0) {
	return FakeEntry{1, 0, typeFlags, 0, reg, 0, 0, 0};
}

// A world of plain values behind every port; every call is logged, every
// read of the fields the original reads lazily is counted.
struct FakeEngine {
	std::vector<bool> present;      // false: a null pointer at that index
	std::vector<FakeEntry> entries;
	std::uint16_t count = 0;
	Handle device = kDevice;
	std::map<std::uint8_t, std::vector<std::uint32_t>> slotResults;  // per call, the last repeats; default 0
	std::vector<std::int32_t> vertexResults{0};
	std::vector<std::int32_t> pixelResults{0};
	std::function<void(FakeEngine&)> afterCall;

	std::vector<Event> log;
	int countReads = 0;
	int arrayReads = 0;
	int gateReads = 0;
	int idReads = 0;
	int deviceReads = 0;
	std::map<std::uint8_t, std::size_t> slotCalls;
	std::size_t vertexCalls = 0;
	std::size_t pixelCalls = 0;

	void Add(const FakeEntry& e) {
		present.push_back(true);
		entries.push_back(e);
		count = static_cast<std::uint16_t>(entries.size());
	}
	void AddNull() {
		present.push_back(false);
		entries.push_back(FakeEntry{});
		count = static_cast<std::uint16_t>(entries.size());
	}
	const FakeEntry& At(Handle entry) const {
		return entries[(entry - EntryHandle(0)) / 0x100u];
	}

	void StoreLastProgram(Handle map, Handle program) {
		log.push_back(StoreEvent(map, program));
	}
	std::uint16_t EntryCount(Handle) {
		++countReads;
		return count;
	}
	Handle Entry(Handle, std::uint32_t index) {
		++arrayReads;
		return present[index] ? EntryHandle(index) : 0;
	}
	std::uint8_t EntryLive(Handle e) const {
		return At(e).live;
	}
	std::uint32_t EntryRegister(Handle e) const {
		return At(e).startRegister;
	}
	std::uint32_t EntryGate(Handle e) {
		++gateReads;
		return At(e).gate;
	}
	std::uint32_t EntryTypeFlags(Handle e) const {
		return At(e).typeFlags;
	}
	std::uint32_t EntryTarget(Handle e) const {
		return At(e).target;
	}
	std::uint32_t EntryRegisterCount(Handle e) const {
		return At(e).registerCount;
	}
	Handle EntryData(Handle e) const {
		return At(e).data;
	}
	std::uint32_t EntryId(Handle e) {
		++idReads;
		return At(e).id;
	}
	Handle Device(Handle) {
		++deviceReads;
		return device;
	}
	Handle Lookup(Handle geometry) {
		log.push_back(LookupEvent(geometry));
		return kExtra;
	}
	template <class T>
	static T PerCall(const std::vector<T>& values, std::size_t& calls) {
		const T v = values.empty() ? T{} : values[calls < values.size() ? calls : values.size() - 1];
		++calls;
		return v;
	}
	std::uint32_t Call(Handle map, const SlotCall& call) {
		log.push_back(SlotEvent(map, call));
		const std::uint32_t r = PerCall(slotResults[call.slotOffset], slotCalls[call.slotOffset]);
		if (afterCall) {
			afterCall(*this);
		}
		return r;
	}
	std::int32_t SetVertexShaderConstantF(Handle d, std::uint32_t start, Handle data, std::uint32_t n) {
		log.push_back(UploadEvent(Kind::Vertex, d, start, data, n));
		const std::int32_t r = PerCall(vertexResults, vertexCalls);
		if (afterCall) {
			afterCall(*this);
		}
		return r;
	}
	std::int32_t SetPixelShaderConstantF(Handle d, std::uint32_t start, Handle data, std::uint32_t n) {
		log.push_back(UploadEvent(Kind::Pixel, d, start, data, n));
		const std::int32_t r = PerCall(pixelResults, pixelCalls);
		if (afterCall) {
			afterCall(*this);
		}
		return r;
	}

	std::uint32_t Run(EntryFilter filter = EntryFilter::kWhole) {
		return ac::Apply(kMap, kInput, filter, *this, *this, *this, *this);
	}

	static constexpr ApplyInput kInput{0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB};
};

SlotCall Eleven(std::uint8_t slot, Handle entry) {
	return ac::ElevenArgumentCall(slot, FakeEngine::kInput, entry);
}
SlotCall Three(std::uint8_t slot, Handle entry) {
	return ac::ThreeArgumentCall(slot, FakeEngine::kInput, entry);
}

std::vector<Event> Prologue() {
	return {StoreEvent(kMap, 0xA1), LookupEvent(0xA2)};
}

std::vector<Event> Then(std::vector<Event> head, const std::vector<Event>& tail) {
	head.insert(head.end(), tail.begin(), tail.end());
	return head;
}

void WholeFlows() {
	Section("B1: empty map - the store, then the lookup on a2, then no entry read; returns 0");
	{
		FakeEngine f;
		const std::uint32_t r = f.Run();
		Check(r == 0, "returns 0");
		Check(f.log == Prologue(), "[map+0x20] = a1 first (0x009A8E3F), then 0x006FF9C0 on the geometry (0x009A8E4D)");
		Check(f.countReads == 1 && f.arrayReads == 0, "the count is read once, the array never");
	}

	Section("B6 + B15: three defined entries - slot +0x54 per entry in index order, count re-read per index");
	{
		FakeEngine f;
		f.Add(Defined(19));
		f.Add(Defined(1));
		f.Add(Defined(23));
		const std::uint32_t r = f.Run();
		Check(r == 0, "every status 0: returns 0");
		Check(f.log == Then(Prologue(), {SlotEvent(kMap, Eleven(0x54, EntryHandle(0))),
											SlotEvent(kMap, Eleven(0x54, EntryHandle(1))),
											SlotEvent(kMap, Eleven(0x54, EntryHandle(2)))}),
			"three slot +0x54 calls with (a1, entry, a2..a10)");
		Check(f.countReads == 4 && f.arrayReads == 3, "count read 4 times (B1 + three B15), the array once per index");
	}

	Section("B2: a null entry in the middle is skipped; the walk goes on");
	{
		FakeEngine f;
		f.Add(Defined(3));
		f.AddNull();
		f.Add(Defined(2));
		f.Run();
		Check(f.log == Then(Prologue(), {SlotEvent(kMap, Eleven(0x54, EntryHandle(0))),
											SlotEvent(kMap, Eleven(0x54, EntryHandle(2)))}),
			"entries 0 and 2 only");
	}

	Section("B3: a dead entry ends the whole walk; a dead first entry means no call at all");
	{
		FakeEngine f;
		f.Add(Defined(19));
		FakeEntry dead = Defined(23);
		dead.live = 0;
		f.Add(dead);
		f.Add(Defined(4));
		const std::uint32_t r = f.Run();
		Check(r == 0 && f.log == Then(Prologue(), {SlotEvent(kMap, Eleven(0x54, EntryHandle(0)))}),
			"entry 0 only, returns 0");
		Check(f.arrayReads == 2, "the array is not read past the dead entry");

		FakeEngine g;
		g.Add(dead);
		g.Add(Defined(19));
		Check(g.Run() == 0 && g.log == Prologue(), "dead first: only the prologue, returns 0");
	}

	Section("B4 / B5: register -1 with gate 0 skipped (flag kept); -1 with gate 1 and register 7 dispatched");
	{
		FakeEngine f;
		f.Add(FakeEntry{1, 19, cr::kTypeDefined, 0, 0xFFFFFFFFu, 0, 0, 0});
		f.Add(FakeEntry{1, 23, cr::kTypeDefined, 0, 0xFFFFFFFFu, 0, 1, 0});
		f.Add(FakeEntry{1, 27, cr::kTypeDefined, 0, 7, 0, 0, 0});
		const std::uint32_t r = f.Run();
		Check(r == 0, "the skip leaves the flag: returns 0");
		Check(f.log == Then(Prologue(), {SlotEvent(kMap, Eleven(0x54, EntryHandle(1))),
											SlotEvent(kMap, Eleven(0x54, EntryHandle(2)))}),
			"entries 1 and 2");
		Check(f.gateReads == 2, "the gate is read for the two -1 entries only");
	}

	Section("B7 / B8 / B9: target 1 -> SetVertexShaderConstantF, 2 -> SetPixelShaderConstantF (device, start, data, count)");
	{
		FakeEngine f;
		f.Add(Constant(1, 4, 3, 0x0C000000u));
		f.Add(Constant(2, 9, 1, 0x0C000010u));
		const std::uint32_t r = f.Run();
		Check(r == 0, "S_OK twice: returns 0");
		Check(f.log == Then(Prologue(), {UploadEvent(Kind::Vertex, kDevice, 4, 0x0C000000u, 3),
											UploadEvent(Kind::Pixel, kDevice, 9, 0x0C000010u, 1)}),
			"the uploads with [entry+0x1C], [entry+0x30], [entry+0x20]");
		Check(f.deviceReads == 2, "[map+0x28] is read per upload");
	}

	Section("B8 + B14: a failed vertex upload clears the flag; the entry after it still runs");
	{
		FakeEngine f;
		f.vertexResults = {static_cast<std::int32_t>(0x8876086Cu)};
		f.Add(Constant(1, 0, 4, 0x0C000000u));
		f.Add(Defined(19));
		const std::uint32_t r = f.Run();
		Check(r == 1, "returns 1");
		Check(f.log.size() == 4 && f.log[3] == SlotEvent(kMap, Eleven(0x54, EntryHandle(1))), "the defined entry still runs");
	}

	Section("B9 + B14: a positive HRESULT is success; a negative one clears the flag");
	{
		FakeEngine f;
		f.pixelResults = {1};
		f.Add(Constant(2, 2, 2, 0x0C000000u));
		Check(f.Run() == 0, "pixel upload returning 1: returns 0");
		FakeEngine g;
		g.pixelResults = {1, static_cast<std::int32_t>(0x80004005u)};
		g.Add(Constant(2, 2, 2, 0x0C000000u));
		g.Add(Constant(2, 6, 2, 0x0C000010u));
		Check(g.Run() == 1, "then one returning 0x80004005: returns 1");
	}

	Section("B9 taken: targets 0, 3 go to slot +0x58 (a1, entry, a10); a status 0x100 clears the flag");
	{
		FakeEngine f;
		f.slotResults[0x58] = {0, 0x100};
		f.Add(Constant(3, 5, 0, 0));
		f.Add(Constant(0, 6, 0, 0));
		const std::uint32_t r = f.Run();
		Check(f.log == Then(Prologue(), {SlotEvent(kMap, Three(0x58, EntryHandle(0))),
											SlotEvent(kMap, Three(0x58, EntryHandle(1)))}),
			"two slot +0x58 calls, no device call");
		Check(r == 1 && f.deviceReads == 0, "returns 1; the device is never read");
	}

	Section("B10: type 0x30000000 -> slot +0x5C with a11 and the one extra-data result; looked up once per apply");
	{
		FakeEngine f;
		f.Add(OfType(0x30000000u, 12));
		f.Add(OfType(0x3000ABCDu, 13));
		const std::uint32_t r = f.Run();
		const SlotCall a0 = ac::AttributeCall(FakeEngine::kInput, EntryHandle(0), kExtra);
		const SlotCall a1 = ac::AttributeCall(FakeEngine::kInput, EntryHandle(1), kExtra);
		Check(r == 0 && f.log == Then(Prologue(), {SlotEvent(kMap, a0), SlotEvent(kMap, a1)}),
			"one lookup, two slot +0x5C calls, both with the same extra data");
		FakeEngine g;
		g.slotResults[0x5C] = {0x10000};
		g.Add(OfType(0x30000000u, 12));
		Check(g.Run() == 1, "B14: a status 0x10000 returns 1");
	}

	Section("B11 / B12 / B13: slots +0x60 (a1, entry, a10), +0x64 (a1, entry, a2, a3, a6..a10), +0x6C (a1, entry, a2..a10)");
	{
		FakeEngine f;
		f.Add(OfType(0x40000000u, 20));
		f.Add(OfType(0x50000000u, 21));
		f.Add(OfType(0x60000000u, 22));
		const std::uint32_t r = f.Run();
		Check(r == 0 && f.log == Then(Prologue(), {SlotEvent(kMap, Three(0x60, EntryHandle(0))),
													  SlotEvent(kMap, ac::OperatorCall(FakeEngine::kInput, EntryHandle(1))),
													  SlotEvent(kMap, Eleven(0x6C, EntryHandle(2)))}),
			"one call each, returns 0");
		FakeEngine g;
		g.slotResults[0x64] = {7};
		g.slotResults[0x6C] = {1};
		g.Add(OfType(0x50000000u, 1));
		g.Add(OfType(0x60000000u, 2));
		Check(g.Run() == 1 && g.log.size() == 4, "B14: non-zero from +0x64 and +0x6C - both still called, returns 1");
		FakeEngine h;
		h.slotResults[0x60] = {0, 5};
		h.Add(OfType(0x40000000u, 1));
		h.Add(OfType(0x40000000u, 2));
		Check(h.Run() == 1, "B14: +0x60's second status 5 returns 1");
	}

	Section("B13 taken: unknown types 0x0, 0x7, 0xF clear the flag without a call; the walk goes on");
	{
		FakeEngine f;
		f.Add(OfType(0x00000005u, 1));
		f.Add(Defined(19));
		f.Add(OfType(0x70000000u, 2));
		f.Add(OfType(0xF0000000u, 3));
		const std::uint32_t r = f.Run();
		Check(r == 1 && f.log == Then(Prologue(), {SlotEvent(kMap, Eleven(0x54, EntryHandle(1)))}),
			"only the defined entry is called; returns 1");
		FakeEngine g;
		g.Add(OfType(0x90000000u, 1));
		Check(g.Run() == 1 && g.log == Prologue(), "a single unknown entry alone returns 1");
	}

	Section("B14: a failure followed by successes - the flag stays cleared");
	{
		FakeEngine f;
		f.slotResults[0x54] = {1, 0, 0};
		f.Add(Defined(19));
		f.Add(Defined(23));
		f.Add(Defined(27));
		Check(f.Run() == 1 && f.log.size() == 5, "returns 1 after three calls");
	}

	Section("C8: register -1 with a non-zero gate reaches the device as StartRegister 0xFFFFFFFF");
	{
		FakeEngine f;
		f.Add(FakeEntry{1, 0, ac::kTypeConstant, 1, 0xFFFFFFFFu, 2, 5, 0x0C000000u});
		f.Run();
		Check(f.log == Then(Prologue(), {UploadEvent(Kind::Vertex, kDevice, 0xFFFFFFFFu, 0x0C000000u, 2)}),
			"SetVertexShaderConstantF(device, 0xFFFFFFFF, data, 2)");
	}

	Section("B15: the count and the array are re-read per index (MOVZX 0x009A9017, MOV ECX,[ESI+0x10] 0x009A8E70)");
	{
		FakeEngine f;
		f.Add(Defined(19));
		f.Add(Defined(23));
		f.Add(Defined(27));
		f.afterCall = [](FakeEngine& e) { e.count = 1; };  // a callee shrinks the map
		f.Run();
		Check(f.log.size() == 3, "a count shrunk by the first callee ends the walk after it");
		FakeEngine g;
		g.Add(Defined(19));
		g.afterCall = [](FakeEngine& e) {
			if (e.entries.size() == 1) {
				e.Add(Defined(23));  // a callee grows the map
			}
		};
		g.Run();
		Check(g.log.size() == 4, "a count grown by the first callee walks on to the new entry");
	}

	Section("A null geometry under kWhole reaches the lookup as the original's (map C18: a precondition)");
	{
		FakeEngine f;
		ApplyInput in = FakeEngine::kInput;
		in.geometry = 0;
		ac::Apply(kMap, in, EntryFilter::kWhole, f, f, f, f);
		Check(f.log.size() == 2 && f.log[1] == LookupEvent(0), "Lookup(0)");
	}
}

void ViewFlows() {
	Section("kView, empty map: the store runs, the lookup does not; returns 0");
	{
		FakeEngine f;
		Check(f.Run(EntryFilter::kView) == 0, "returns 0");
		Check(f.log == std::vector<Event>{StoreEvent(kMap, 0xA1)}, "only [map+0x20] = a1");
	}

	Section("kView: only defined entries with a view id are dispatched, with the original's 11 arguments");
	{
		FakeEngine f;
		f.Add(Defined(19));
		f.Add(Defined(1));
		f.Add(Constant(1, 0, 4, 0x0C000000u));
		f.Add(Constant(3, 0, 0, 0));
		f.Add(OfType(0x30000000u));
		f.Add(OfType(0x40000000u));
		f.Add(OfType(0x50000000u));
		f.Add(OfType(0x60000000u));
		f.Add(OfType(0x90000000u));
		f.Add(Defined(31));
		const std::uint32_t r = f.Run(EntryFilter::kView);
		Check(f.log == std::vector<Event>{StoreEvent(kMap, 0xA1), SlotEvent(kMap, Eleven(0x54, EntryHandle(0))),
						   SlotEvent(kMap, Eleven(0x54, EntryHandle(9)))},
			"store, then slot +0x54 for ids 19 and 31 only; no lookup, no device, no other slot");
		Check(r == 0, "the unknown type is skipped without clearing the flag: returns 0");
		Check(f.idReads == 3 && f.deviceReads == 0, "the id is read for the three defined entries only");
	}

	Section("kView: the flag is over the dispatched entries only");
	{
		FakeEngine f;
		f.slotResults[0x54] = {0x80000000u};
		f.Add(Defined(23));
		Check(f.Run(EntryFilter::kView) == 1, "a dispatched view entry returning 0x80000000: returns 1");
		FakeEngine g;
		g.vertexResults = {-1};
		g.slotResults[0x60] = {1};
		g.Add(Constant(1, 0, 4, 0));
		g.Add(OfType(0x40000000u));
		g.Add(Defined(19));
		Check(g.Run(EntryFilter::kView) == 0 && g.log.size() == 2, "failing entries that are not dispatched: returns 0");
	}

	Section("kView: the original's null, liveness and register tests run first");
	{
		FakeEngine f;
		f.AddNull();
		f.Add(FakeEntry{1, 19, cr::kTypeDefined, 0, 0xFFFFFFFFu, 0, 0, 0});  // B5: skipped
		f.Add(FakeEntry{1, 23, cr::kTypeDefined, 0, 0xFFFFFFFFu, 0, 1, 0});  // dispatched
		FakeEntry dead = Defined(27);
		dead.live = 0;
		f.Add(dead);
		f.Add(Defined(31));
		f.Run(EntryFilter::kView);
		Check(f.log == std::vector<Event>{StoreEvent(kMap, 0xA1), SlotEvent(kMap, Eleven(0x54, EntryHandle(2)))},
			"null skipped, -1 / gate 0 skipped, -1 / gate 1 dispatched, the dead entry ends the walk");
		Check(f.idReads == 1, "the id is not read for the entry the register gate skipped");
	}
}

// ---- The differential gate: the original's trace -------------------------------------

// The synthetic memory of the cases (tools/difftest/cases/0x009A8E30.json, "note").
constexpr std::uint32_t kAny = 0xFFFF0002u;   // ECX is not an input of this callee
constexpr Handle kEmuMap = 0x10000000u;       // ECX of every case
constexpr Handle kEmuDevice = 0x10002000u;    // [map+0x28]
constexpr Handle kEmuExtra = 0x1000E000u;     // 0x006FF9C0's stub result
constexpr Handle kEmuName = 0x00A8A9BCu;      // [0x00B29F84] (data.json)
constexpr std::uint32_t kGetExtraData = 0x006FF9C0u;
constexpr std::uint32_t kVertexStub = 0x20003000u;
constexpr std::uint32_t kPixelStub = 0x20003100u;
constexpr ApplyInput kEmuInput{0x11000001u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u, 0x11000006u,
	0x11000007u, 0x11000008u, 0x11000009u, 0x1100000Au, 0x1100000Bu};

constexpr std::uint32_t SlotStub(std::uint8_t slot) {
	return 0x20000000u + 0x100u * slot;
}
constexpr std::uint32_t Capture(std::uint8_t slot) {
	return 0x10010000u + 0x10u * slot;
}

// The return address of each call site (asm.s: the instruction after the CALL).
constexpr std::uint32_t SlotReturn(std::uint8_t slot) {
	switch (slot) {
	case 0x54:
	case 0x6C: return 0x009A900Au;  // the shared CALL EDX 0x009A9008
	case 0x58: return 0x009A8F40u;
	case 0x5C: return 0x009A8F87u;
	case 0x60: return 0x009A8FA0u;
	case 0x64: return 0x009A8FD0u;
	default: return 0;
	}
}
std::uint8_t SlotOfStub(std::uint32_t stub) {
	for (std::uint8_t slot : {ac::kSlotDefined, ac::kSlotConstant, ac::kSlotAttribute, ac::kSlotGlobal,
			 ac::kSlotOperator, ac::kSlotObject}) {
		if (stub == SlotStub(slot)) {
			return slot;
		}
	}
	return 0;
}
std::uint8_t ArgumentCount(std::uint32_t callee) {
	switch (callee) {
	case kGetExtraData: return 1;
	case kVertexStub:
	case kPixelStub: return 4;
	default: break;
	}
	switch (SlotOfStub(callee)) {
	case 0x54:
	case 0x6C: return 11;
	case 0x58:
	case 0x60: return 3;
	case 0x5C: return 13;
	case 0x64: return 9;
	default: return 0xFF;
	}
}

struct EntryInput {
	Handle address;
	std::uint8_t live;
	std::uint32_t id;
	std::uint32_t typeFlags;
	std::uint32_t target;
	std::uint32_t startRegister;
	std::uint32_t registerCount;
	std::uint32_t gate;
	Handle data;
};

struct CaseInputs {
	std::uint16_t count;
	std::vector<Handle> array;  // the entry pointer array [map+0x10]
	std::vector<EntryInput> entries;
	std::uint32_t slot54;  // the raw-code stubs' fixed results
	std::vector<std::uint32_t> slot58;  // per call, the last repeats
	std::uint32_t slot5C;
	std::vector<std::uint32_t> slot60;
	std::uint32_t slot64;
	std::uint32_t slot6C;
	std::vector<std::uint32_t> vertex;  // HRESULT bit patterns per call
	std::vector<std::uint32_t> pixel;
};

struct TracedCall {
	std::uint32_t callee;
	std::uint32_t from;
	std::uint32_t ecx;
	std::uint32_t args[6];
};

struct RecordedWrite {
	std::uint32_t at;
	std::uint32_t address;
	std::uint32_t size;
	std::uint32_t value;
};

struct EmulatorCase {
	const char* name;
	CaseInputs input;
	std::uint32_t eax;
	std::vector<TracedCall> calls;
	std::vector<RecordedWrite> writes;
};

// Transcribed from tools/difftest/cases/0x009A8E30.json (inputs) and
// tools/difftest/expected/0x009A8E30.json (calls, writes, EAX), 2026-11-04.
const std::vector<EmulatorCase> kCases = {
	{
		"B1 empty map: the lookup and the program store still run; returns 0",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{0, {}, {}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u}},
	},
	{
		"B6 + B15: three defined entries (ids 19, 1, 23), slot +0x54 returns 0; returns 0",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{3, {0x10004000u, 0x10004040u, 0x10004080u}, {{0x10004000u, 1, 0x00000013u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000001u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008010u}, {0x10004080u, 1, 0x00000017u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008020u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004040u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004080u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au}},
	},
	{
		"B2: a null entry in the middle is skipped",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{3, {0x10004000u, 0x00000000u, 0x10004080u}, {{0x10004000u, 1, 0x00000003u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004080u, 1, 0x00000002u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008020u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004080u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au}},
	},
	{
		"B3: a dead entry ends the walk; the live one after it is not applied",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{3, {0x10004000u, 0x10004040u, 0x10004080u}, {{0x10004000u, 1, 0x00000013u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004040u, 0, 0x00000017u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008010u}, {0x10004080u, 1, 0x00000004u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008020u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au}},
	},
	{
		"B3: a dead first entry: no slot call at all",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{2, {0x10004000u, 0x10004040u}, {{0x10004000u, 0, 0x00000013u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000017u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008010u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u}},
	},
	{
		"B4 / B5: register -1 with gate 0 skipped, register -1 with gate 1 and register 7 dispatched",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{3, {0x10004000u, 0x10004040u, 0x10004080u}, {{0x10004000u, 1, 0x00000013u, 0x20000000u, 0x00000000u, 0xFFFFFFFFu, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000017u, 0x20000000u, 0x00000000u, 0xFFFFFFFFu, 0x00000004u, 0x00000001u, 0x10008010u}, {0x10004080u, 1, 0x0000001Bu, 0x20000000u, 0x00000000u, 0x00000007u, 0x00000004u, 0x00000000u, 0x10008020u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004040u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004080u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au}},
	},
	{
		"B7 / B8 / B9: type 0x10000000 target 1 -> SetVertexShaderConstantF, target 2 -> SetPixelShaderConstantF, both S_OK; returns 0",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{2, {0x10004000u, 0x10004040u}, {{0x10004000u, 1, 0x00000000u, 0x10000000u, 0x00000001u, 0x00000004u, 0x00000003u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000000u, 0x10000000u, 0x00000002u, 0x00000009u, 0x00000001u, 0x00000000u, 0x10008010u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20003000u, 0x009A8EF0u, 0x10002000u, {0x10002000u, 0x00000004u, 0x10008000u, 0x00000003u, 0x00000000u, 0x00000000u}},
			{0x20003100u, 0x009A8F26u, 0x10002000u, {0x10002000u, 0x00000009u, 0x10008010u, 0x00000001u, 0x00000000u, 0x00000000u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u}},
	},
	{
		"B8 + B14: a failed vertex upload (0x8876086C) clears the flag; the defined entry after it still runs",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{2, {0x10004000u, 0x10004040u}, {{0x10004000u, 1, 0x00000000u, 0x10000000u, 0x00000001u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000013u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008010u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x8876086Cu}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000001u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20003000u, 0x009A8EF0u, 0x10002000u, {0x10002000u, 0x00000000u, 0x10008000u, 0x00000004u, 0x00000000u, 0x00000000u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004040u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au}},
	},
	{
		"B9 + B14: pixel upload returning 1 (positive) keeps the flag, the next returning 0x80004005 clears it",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{2, {0x10004000u, 0x10004040u}, {{0x10004000u, 1, 0x00000000u, 0x10000000u, 0x00000002u, 0x00000002u, 0x00000002u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000000u, 0x10000000u, 0x00000002u, 0x00000006u, 0x00000002u, 0x00000000u, 0x10008010u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000001u, 0x80004005u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000001u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20003100u, 0x009A8F26u, 0x10002000u, {0x10002000u, 0x00000002u, 0x10008000u, 0x00000002u, 0x00000000u, 0x00000000u}},
			{0x20003100u, 0x009A8F26u, 0x10002000u, {0x10002000u, 0x00000006u, 0x10008010u, 0x00000002u, 0x00000000u, 0x00000000u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u}},
	},
	{
		"B9: targets 3 and 0 go to slot +0x58 (a1, entry, a10); its second result 0x100 clears the flag",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{2, {0x10004000u, 0x10004040u}, {{0x10004000u, 1, 0x00000000u, 0x10000000u, 0x00000003u, 0x00000005u, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000000u, 0x10000000u, 0x00000000u, 0x00000006u, 0x00000004u, 0x00000000u, 0x10008010u}}, 0x00000000u, {0x00000000u, 0x00000100u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000001u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005800u, 0x009A8F40u, 0x10000000u, {0x11000001u, 0x10004000u, 0x1100000Au, 0x00000000u, 0x00000000u, 0x00000000u}},
			{0x20005800u, 0x009A8F40u, 0x10000000u, {0x11000001u, 0x10004040u, 0x1100000Au, 0x00000000u, 0x00000000u, 0x00000000u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u}},
	},
	{
		"B10: type 0x30000000 -> slot +0x5C (a1, entry, a2..a11, extra data); returns 0",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{1, {0x10004000u}, {{0x10004000u, 1, 0x00000007u, 0x30000000u, 0x00000000u, 0x0000000Cu, 0x00000004u, 0x00000000u, 0x10008000u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005C00u, 0x009A8F87u, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005C04u, 0x100105C0u, 4, 0x11000006u},
			{0x20005C0Du, 0x100105C4u, 4, 0x11000007u},
			{0x20005C16u, 0x100105C8u, 4, 0x11000008u},
			{0x20005C1Fu, 0x100105CCu, 4, 0x11000009u},
			{0x20005C28u, 0x100105D0u, 4, 0x1100000Au},
			{0x20005C31u, 0x100105D4u, 4, 0x1100000Bu},
			{0x20005C3Au, 0x100105D8u, 4, 0x1000E000u}},
	},
	{
		"B10 + B14: slot +0x5C returning 0x10000 clears the flag",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{1, {0x10004000u}, {{0x10004000u, 1, 0x00000000u, 0x30000000u, 0x00000000u, 0x0000000Cu, 0x00000004u, 0x00000000u, 0x10008000u}}, 0x00000000u, {0x00000000u}, 0x00010000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000001u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005C00u, 0x009A8F87u, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005C04u, 0x100105C0u, 4, 0x11000006u},
			{0x20005C0Du, 0x100105C4u, 4, 0x11000007u},
			{0x20005C16u, 0x100105C8u, 4, 0x11000008u},
			{0x20005C1Fu, 0x100105CCu, 4, 0x11000009u},
			{0x20005C28u, 0x100105D0u, 4, 0x1100000Au},
			{0x20005C31u, 0x100105D4u, 4, 0x1100000Bu},
			{0x20005C3Au, 0x100105D8u, 4, 0x1000E000u}},
	},
	{
		"B11: type 0x40000000 -> slot +0x60 (a1, entry, a10); returns 0",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{1, {0x10004000u}, {{0x10004000u, 1, 0x00000000u, 0x40000000u, 0x00000000u, 0x00000014u, 0x00000004u, 0x00000000u, 0x10008000u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20006000u, 0x009A8FA0u, 0x10000000u, {0x11000001u, 0x10004000u, 0x1100000Au, 0x00000000u, 0x00000000u, 0x00000000u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u}},
	},
	{
		"B12: type 0x50000000 -> slot +0x64 (a1, entry, a2, a3, a6..a10); returns 0",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{1, {0x10004000u}, {{0x10004000u, 1, 0x00000000u, 0x50000000u, 0x00000000u, 0x00000015u, 0x00000004u, 0x00000000u, 0x10008000u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20006400u, 0x009A8FD0u, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000006u, 0x11000007u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20006404u, 0x10010640u, 4, 0x11000008u},
			{0x2000640Du, 0x10010644u, 4, 0x11000009u},
			{0x20006416u, 0x10010648u, 4, 0x1100000Au}},
	},
	{
		"B13: type 0x60000000 -> slot +0x6C (a1, entry, a2..a10); returns 0",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{1, {0x10004000u}, {{0x10004000u, 1, 0x00000000u, 0x60000000u, 0x00000000u, 0x00000016u, 0x00000004u, 0x00000000u, 0x10008000u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20006C00u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20006C04u, 0x100106C0u, 4, 0x11000006u},
			{0x20006C0Du, 0x100106C4u, 4, 0x11000007u},
			{0x20006C16u, 0x100106C8u, 4, 0x11000008u},
			{0x20006C1Fu, 0x100106CCu, 4, 0x11000009u},
			{0x20006C28u, 0x100106D0u, 4, 0x1100000Au}},
	},
	{
		"B13: unknown types 0x0, 0x7, 0xF clear the flag without a call; the defined entry between runs",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{4, {0x10004000u, 0x10004040u, 0x10004080u, 0x100040C0u}, {{0x10004000u, 1, 0x00000000u, 0x00000005u, 0x00000000u, 0x00000001u, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000013u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008010u}, {0x10004080u, 1, 0x00000000u, 0x70000000u, 0x00000000u, 0x00000002u, 0x00000004u, 0x00000000u, 0x10008020u}, {0x100040C0u, 1, 0x00000000u, 0xF0000000u, 0x00000000u, 0x00000003u, 0x00000004u, 0x00000000u, 0x10008030u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000001u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004040u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au}},
	},
	{
		"B14: slot +0x54 returning 0x80000000 clears the flag; the type's low 28 bits are ignored",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{1, {0x10004000u}, {{0x10004000u, 1, 0x00000017u, 0x2ABCDEF0u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008000u}}, 0x80000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000001u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au}},
	},
	{
		"B14: slots +0x64 and +0x6C returning non-zero (7, 1)",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{2, {0x10004000u, 0x10004040u}, {{0x10004000u, 1, 0x00000000u, 0x50000000u, 0x00000000u, 0x00000001u, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000000u, 0x60000000u, 0x00000000u, 0x00000002u, 0x00000004u, 0x00000000u, 0x10008010u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000007u, 0x00000001u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000001u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20006400u, 0x009A8FD0u, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000006u, 0x11000007u}},
			{0x20006C00u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004040u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20006404u, 0x10010640u, 4, 0x11000008u},
			{0x2000640Du, 0x10010644u, 4, 0x11000009u},
			{0x20006416u, 0x10010648u, 4, 0x1100000Au},
			{0x20006C04u, 0x100106C0u, 4, 0x11000006u},
			{0x20006C0Du, 0x100106C4u, 4, 0x11000007u},
			{0x20006C16u, 0x100106C8u, 4, 0x11000008u},
			{0x20006C1Fu, 0x100106CCu, 4, 0x11000009u},
			{0x20006C28u, 0x100106D0u, 4, 0x1100000Au}},
	},
	{
		"C8: register -1 with gate 5 reaches SetVertexShaderConstantF as StartRegister 0xFFFFFFFF",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{1, {0x10004000u}, {{0x10004000u, 1, 0x00000000u, 0x10000000u, 0x00000001u, 0xFFFFFFFFu, 0x00000002u, 0x00000005u, 0x10008000u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20003000u, 0x009A8EF0u, 0x10002000u, {0x10002000u, 0xFFFFFFFFu, 0x10008000u, 0x00000002u, 0x00000000u, 0x00000000u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u}},
	},
	{
		"All kinds in one map: defined 19, vertex, pixel, program, attribute, global (returns 1), operator, object, unknown 0x9, defined 31",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{10, {0x10004000u, 0x10004040u, 0x10004080u, 0x100040C0u, 0x10004100u, 0x10004140u, 0x10004180u, 0x100041C0u, 0x10004200u, 0x10004240u}, {{0x10004000u, 1, 0x00000013u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000000u, 0x10000000u, 0x00000001u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008010u}, {0x10004080u, 1, 0x00000000u, 0x10000000u, 0x00000002u, 0x00000001u, 0x00000001u, 0x00000000u, 0x10008020u}, {0x100040C0u, 1, 0x00000000u, 0x10000000u, 0x00000007u, 0x00000002u, 0x00000004u, 0x00000000u, 0x10008030u}, {0x10004100u, 1, 0x00000000u, 0x30000000u, 0x00000000u, 0x00000003u, 0x00000004u, 0x00000000u, 0x10008040u}, {0x10004140u, 1, 0x00000000u, 0x40000000u, 0x00000000u, 0x00000004u, 0x00000004u, 0x00000000u, 0x10008050u}, {0x10004180u, 1, 0x00000000u, 0x50000000u, 0x00000000u, 0x00000005u, 0x00000004u, 0x00000000u, 0x10008060u}, {0x100041C0u, 1, 0x00000000u, 0x60000000u, 0x00000000u, 0x00000006u, 0x00000004u, 0x00000000u, 0x10008070u}, {0x10004200u, 1, 0x00000000u, 0x90000000u, 0x00000000u, 0x00000007u, 0x00000004u, 0x00000000u, 0x10008080u}, {0x10004240u, 1, 0x0000001Fu, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008090u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000001u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000001u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}},
			{0x20003000u, 0x009A8EF0u, 0x10002000u, {0x10002000u, 0x00000000u, 0x10008010u, 0x00000004u, 0x00000000u, 0x00000000u}},
			{0x20003100u, 0x009A8F26u, 0x10002000u, {0x10002000u, 0x00000001u, 0x10008020u, 0x00000001u, 0x00000000u, 0x00000000u}},
			{0x20005800u, 0x009A8F40u, 0x10000000u, {0x11000001u, 0x100040C0u, 0x1100000Au, 0x00000000u, 0x00000000u, 0x00000000u}},
			{0x20005C00u, 0x009A8F87u, 0x10000000u, {0x11000001u, 0x10004100u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}},
			{0x20006000u, 0x009A8FA0u, 0x10000000u, {0x11000001u, 0x10004140u, 0x1100000Au, 0x00000000u, 0x00000000u, 0x00000000u}},
			{0x20006400u, 0x009A8FD0u, 0x10000000u, {0x11000001u, 0x10004180u, 0x11000002u, 0x11000003u, 0x11000006u, 0x11000007u}},
			{0x20006C00u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x100041C0u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004240u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au},
			{0x20005C04u, 0x100105C0u, 4, 0x11000006u},
			{0x20005C0Du, 0x100105C4u, 4, 0x11000007u},
			{0x20005C16u, 0x100105C8u, 4, 0x11000008u},
			{0x20005C1Fu, 0x100105CCu, 4, 0x11000009u},
			{0x20005C28u, 0x100105D0u, 4, 0x1100000Au},
			{0x20005C31u, 0x100105D4u, 4, 0x1100000Bu},
			{0x20005C3Au, 0x100105D8u, 4, 0x1000E000u},
			{0x20006404u, 0x10010640u, 4, 0x11000008u},
			{0x2000640Du, 0x10010644u, 4, 0x11000009u},
			{0x20006416u, 0x10010648u, 4, 0x1100000Au},
			{0x20006C04u, 0x100106C0u, 4, 0x11000006u},
			{0x20006C0Du, 0x100106C4u, 4, 0x11000007u},
			{0x20006C16u, 0x100106C8u, 4, 0x11000008u},
			{0x20006C1Fu, 0x100106CCu, 4, 0x11000009u},
			{0x20006C28u, 0x100106D0u, 4, 0x1100000Au},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au}},
	},
	{
		"B15: the walk stops at the count (2), not at the array's third live entry",
		// inputs (tools/difftest/cases/0x009A8E30.json)
		{2, {0x10004000u, 0x10004040u, 0x10004080u}, {{0x10004000u, 1, 0x00000013u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008000u}, {0x10004040u, 1, 0x00000017u, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008010u}, {0x10004080u, 1, 0x0000001Bu, 0x20000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0x10008020u}}, 0x00000000u, {0x00000000u}, 0x00000000u, {0x00000000u}, 0x00000000u, 0x00000000u, {0x00000000u}, {0x00000000u}},
		// the original's result (tools/difftest/expected/0x009A8E30.json)
		0x00000000u,
		{
			{0x006FF9C0u, 0x009A8E52u, 0x11000002u, {0x00A8A9BCu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x7E000000u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004000u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}},
			{0x20005400u, 0x009A900Au, 0x10000000u, {0x11000001u, 0x10004040u, 0x11000002u, 0x11000003u, 0x11000004u, 0x11000005u}}},
		{
			{0x009A8E3Fu, 0x10000020u, 4, 0x11000001u},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au},
			{0x20005404u, 0x10010540u, 4, 0x11000006u},
			{0x2000540Du, 0x10010544u, 4, 0x11000007u},
			{0x20005416u, 0x10010548u, 4, 0x11000008u},
			{0x2000541Fu, 0x1001054Cu, 4, 0x11000009u},
			{0x20005428u, 0x10010550u, 4, 0x1100000Au}},
	},
};

// One call with every argument the callee takes.
struct FullCall {
	std::uint32_t callee = 0;
	std::uint32_t from = 0;
	std::uint32_t ecx = 0;
	std::uint8_t argumentCount = 0;
	std::uint32_t args[13] = {};

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

bool IsBodyWrite(const RecordedWrite& w) {
	return w.at >= ac::engine::kApplyConstantsAddress && w.at < 0x009A903Du;
}

// The original's calls with the capture writes put back as arguments 7..N.
// ok turns false when a capture write does not sit where the call order puts it.
std::vector<FullCall> Merge(const EmulatorCase& c, bool& ok) {
	std::vector<FullCall> out;
	std::vector<RecordedWrite> captures;
	for (const RecordedWrite& w : c.writes) {
		if (!IsBodyWrite(w)) {
			captures.push_back(w);
		}
	}
	std::size_t w = 0;
	for (const TracedCall& t : c.calls) {
		FullCall f{t.callee, t.from, t.ecx, ArgumentCount(t.callee), {}};
		ok = ok && f.argumentCount != 0xFF;
		for (std::size_t i = 0; i < 6; ++i) {
			f.args[i] = t.args[i];
		}
		for (std::uint8_t i = 6; i < f.argumentCount && f.argumentCount != 0xFF; ++i) {
			ok = ok && w < captures.size() &&
				captures[w].address == Capture(SlotOfStub(t.callee)) + 4u * (i - 6u) && captures[w].size == 4;
			f.args[i] = w < captures.size() ? captures[w].value : 0;
			++w;
		}
		if (t.callee == kVertexStub || t.callee == kPixelStub) {
			f.ecx = kAny;
		}
		out.push_back(f);
	}
	ok = ok && w == captures.size();
	return out;
}

// The ports over the case's inputs, each call recorded in the emulator's terms.
struct EmulatorPorts {
	const CaseInputs& in;
	std::vector<FullCall> calls;
	std::vector<RecordedWrite> stores;
	std::size_t slot58Calls = 0;
	std::size_t slot60Calls = 0;
	std::size_t vertexCalls = 0;
	std::size_t pixelCalls = 0;

	const EntryInput& At(Handle entry) const {
		for (const EntryInput& e : in.entries) {
			if (e.address == entry) {
				return e;
			}
		}
		static const EntryInput none{};
		return none;
	}
	static std::uint32_t PerCall(const std::vector<std::uint32_t>& v, std::size_t& n) {
		const std::uint32_t r = v.empty() ? 0 : v[n < v.size() ? n : v.size() - 1];
		++n;
		return r;
	}

	void StoreLastProgram(Handle map, Handle program) {
		stores.push_back(RecordedWrite{0x009A8E3Fu, map + 0x20u, 4, program});
	}
	std::uint16_t EntryCount(Handle) const {
		return in.count;
	}
	Handle Entry(Handle, std::uint32_t index) const {
		return index < in.array.size() ? in.array[index] : 0xBADBAD00u;
	}
	std::uint8_t EntryLive(Handle e) const {
		return At(e).live;
	}
	std::uint32_t EntryRegister(Handle e) const {
		return At(e).startRegister;
	}
	std::uint32_t EntryGate(Handle e) const {
		return At(e).gate;
	}
	std::uint32_t EntryTypeFlags(Handle e) const {
		return At(e).typeFlags;
	}
	std::uint32_t EntryTarget(Handle e) const {
		return At(e).target;
	}
	std::uint32_t EntryRegisterCount(Handle e) const {
		return At(e).registerCount;
	}
	Handle EntryData(Handle e) const {
		return At(e).data;
	}
	std::uint32_t EntryId(Handle e) const {
		return At(e).id;
	}
	Handle Device(Handle) const {
		return kEmuDevice;
	}
	Handle Lookup(Handle geometry) {
		calls.push_back(FullCall{kGetExtraData, 0x009A8E52u, geometry, 1, {kEmuName}});
		return kEmuExtra;
	}
	std::uint32_t Call(Handle map, const SlotCall& call) {
		FullCall f{SlotStub(call.slotOffset), SlotReturn(call.slotOffset), map, call.argumentCount, {}};
		for (std::uint8_t i = 0; i < call.argumentCount; ++i) {
			f.args[i] = call.arguments[i];
		}
		calls.push_back(f);
		switch (call.slotOffset) {
		case 0x54: return in.slot54;
		case 0x58: return PerCall(in.slot58, slot58Calls);
		case 0x5C: return in.slot5C;
		case 0x60: return PerCall(in.slot60, slot60Calls);
		case 0x64: return in.slot64;
		case 0x6C: return in.slot6C;
		default: return 0xBADu;
		}
	}
	std::int32_t SetVertexShaderConstantF(Handle device, std::uint32_t start, Handle data, std::uint32_t count) {
		calls.push_back(FullCall{kVertexStub, 0x009A8EF0u, kAny, 4, {device, start, data, count}});
		return static_cast<std::int32_t>(PerCall(in.vertex, vertexCalls));
	}
	std::int32_t SetPixelShaderConstantF(Handle device, std::uint32_t start, Handle data, std::uint32_t count) {
		calls.push_back(FullCall{kPixelStub, 0x009A8F26u, kAny, 4, {device, start, data, count}});
		return static_cast<std::int32_t>(PerCall(in.pixel, pixelCalls));
	}
};

// The kView expectation from the original's trace: the slot +0x54 calls of
// entries whose id is a view id, nothing else (ConstantReapply.h).
std::vector<FullCall> ViewSubset(const std::vector<FullCall>& original, const EmulatorPorts& lookup) {
	std::vector<FullCall> out;
	for (const FullCall& f : original) {
		if (f.callee == SlotStub(ac::kSlotDefined) && cr::IsViewId(lookup.At(f.args[1]).id)) {
			out.push_back(f);
		}
	}
	return out;
}

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
	Section("Emulator gate: the transcription covers 21 cases");
	Check(kCases.size() == 21, "21 cases, as tools/difftest/expected/0x009A8E30.json");
	for (const EmulatorCase& c : kCases) {
		Section(c.name);
		bool merged = true;
		const std::vector<FullCall> original = Merge(c, merged);
		Check(merged, "every capture write sits where the original's call order puts it");
		std::vector<RecordedWrite> bodyWrites;
		for (const RecordedWrite& w : c.writes) {
			if (IsBodyWrite(w)) {
				bodyWrites.push_back(w);
			}
		}
		const bool oneStore = bodyWrites.size() == 1 && bodyWrites[0].at == 0x009A8E3Fu &&
			bodyWrites[0].address == kEmuMap + 0x20u && bodyWrites[0].size == 4 &&
			bodyWrites[0].value == kEmuInput.program;
		Check(oneStore, "the body's one write: [this+0x20] = a1 at 0x009A8E3F");

		EmulatorPorts whole{c.input, {}, {}};
		const std::uint32_t eax = ac::Apply(kEmuMap, kEmuInput, EntryFilter::kWhole, whole, whole, whole, whole);
		Check(whole.calls == original, "kWhole: the original's calls, in order, with every argument");
		if (!(whole.calls == original)) {
			PrintFirstDifference(whole.calls, original);
		}
		Check(whole.stores.size() == 1 && whole.stores[0].address == bodyWrites[0].address &&
				  whole.stores[0].value == bodyWrites[0].value,
			"kWhole: the same store");
		Check(eax == c.eax, "kWhole: the original's EAX");

		EmulatorPorts view{c.input, {}, {}};
		const std::uint32_t viewEax = ac::Apply(kEmuMap, kEmuInput, EntryFilter::kView, view, view, view, view);
		const std::vector<FullCall> subset = ViewSubset(original, view);
		Check(view.calls == subset, "kView: the original's slot +0x54 calls of view ids only, no lookup");
		if (!(view.calls == subset)) {
			PrintFirstDifference(view.calls, subset);
		}
		Check(view.stores.size() == 1 && view.stores[0].value == kEmuInput.program, "kView: the same store");
		const std::uint32_t wantView = ac::Result(subset.empty() || c.input.slot54 == 0);
		Check(viewEax == wantView, "kView: the result over the dispatched calls' statuses");
	}
}

// ---- The engine ports over native stand-ins -----------------------------------------
//
// NiD3DShaderConstantMap_ApplyConstants.h's EnginePorts read the views and
// call through the vtables with the original's conventions (thiscall map
// slots and lookup, stdcall COM uploads). Here those vtables point at native
// functions with the same conventions that log what they receive, so the
// offsets, the conventions and the argument order of the engine side run for
// real (the tests are 32-bit, tests/CMakeLists.txt). Not the game: the
// stand-ins only record.

namespace en = ac::engine;

struct NativeCall {
	int kind;  // the slot offset 0x54..0x6C, or one of the kinds below
	std::uint32_t self;
	std::vector<std::uint32_t> args;
	bool operator==(const NativeCall&) const = default;
};
constexpr int kNativeLookup = 0x100;
constexpr int kNativeVertex = 0x101;
constexpr int kNativePixel = 0x102;

std::vector<NativeCall>& NativeLog() {
	static std::vector<NativeCall> log;
	return log;
}

std::uint32_t H(const void* p) {
	return en::ToHandle(p);
}

template <int Slot>
std::uint32_t __thiscall Native3(void* self, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
	NativeLog().push_back({Slot, H(self), {a, b, c}});
	return 0;
}
std::uint32_t __thiscall Native9(void* self, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d,
	std::uint32_t e, std::uint32_t f, std::uint32_t g, std::uint32_t h, std::uint32_t i) {
	NativeLog().push_back({0x64, H(self), {a, b, c, d, e, f, g, h, i}});
	return 0;
}
template <int Slot>
std::uint32_t __thiscall Native11(void* self, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d,
	std::uint32_t e, std::uint32_t f, std::uint32_t g, std::uint32_t h, std::uint32_t i, std::uint32_t j,
	std::uint32_t k) {
	NativeLog().push_back({Slot, H(self), {a, b, c, d, e, f, g, h, i, j, k}});
	return Slot == 0x6C ? 1u : 0u;  // the object slot fails
}
std::uint32_t __thiscall Native13(void* self, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d,
	std::uint32_t e, std::uint32_t f, std::uint32_t g, std::uint32_t h, std::uint32_t i, std::uint32_t j,
	std::uint32_t k, std::uint32_t l, std::uint32_t m) {
	NativeLog().push_back({0x5C, H(self), {a, b, c, d, e, f, g, h, i, j, k, l, m}});
	return 0;
}
void* __thiscall NativeGetExtraData(void* object, const char* name) {
	NativeLog().push_back({kNativeLookup, H(object), {H(name)}});
	static int extra = 0;
	return &extra;
}
std::int32_t __stdcall NativeSetVertex(void* device, std::uint32_t start, const float* data, std::uint32_t count) {
	NativeLog().push_back({kNativeVertex, H(device), {start, H(data), count}});
	return 0;
}
std::int32_t __stdcall NativeSetPixel(void* device, std::uint32_t start, const float* data, std::uint32_t count) {
	NativeLog().push_back({kNativePixel, H(device), {start, H(data), count}});
	return 0;
}

void NativeEnginePorts() {
	static en::ShaderConstantMapVtableView mapVtable{};
	mapVtable.defined = Native11<0x54>;
	mapVtable.constant = Native3<0x58>;
	mapVtable.attribute = Native13;
	mapVtable.global = Native3<0x60>;
	mapVtable.operator_ = Native9;
	mapVtable.object = Native11<0x6C>;
	static en::DeviceVtableView deviceVtable{};
	deviceVtable.setVertexShaderConstantF = NativeSetVertex;
	deviceVtable.setPixelShaderConstantF = NativeSetPixel;
	static en::DeviceView device{&deviceVtable};

	static const float dataV[8] = {};
	static const float dataP[4] = {};
	static en::ShaderConstantEntryView e[9]{};
	auto set = [](en::ShaderConstantEntryView& x, std::uint32_t id, std::uint32_t type, std::uint32_t target,
				   std::uint32_t reg, std::uint32_t count, const float* data) {
		x.live = 1;
		x.id = id;
		x.typeFlags = type;
		x.target = target;
		x.startRegister = reg;
		x.registerCount = count;
		x.gate = 0;
		x.data = data;
	};
	set(e[0], 19, 0x20000000u, 0, 0, 4, nullptr);
	set(e[1], 0, 0x10000000u, 1, 8, 2, dataV);
	set(e[2], 0, 0x10000000u, 2, 3, 1, dataP);
	set(e[3], 0, 0x10000000u, 5, 0, 0, nullptr);
	set(e[4], 0, 0x30000000u, 0, 0, 0, nullptr);
	set(e[5], 0, 0x40000000u, 0, 0, 0, nullptr);
	set(e[6], 0, 0x50000000u, 0, 0, 0, nullptr);
	set(e[7], 0, 0x60000000u, 0, 0, 0, nullptr);
	set(e[8], 1, 0x20000000u, 0, 0, 4, nullptr);
	static en::ShaderConstantEntryView* array[9] = {&e[0], &e[1], &e[2], &e[3], &e[4], &e[5], &e[6], &e[7], &e[8]};

	static en::ShaderConstantMapView map{};
	map.vtable = &mapVtable;
	map.entries = array;
	map.entryCount = 9;
	map.lastProgram = 0xDEADBEEFu;
	map.device = &device;

	static const char kName[] = "__NDL_SCMData";
	static const char* nameSlot = kName;
	int geometry = 0;
	const std::uint32_t g = H(&geometry);
	const std::uint32_t m = H(&map);
	const std::uint32_t dev = H(&device);
	const std::uint32_t extra = H(NativeGetExtraData(nullptr, nullptr));
	NativeLog().clear();
	auto E = [](int i) { return H(&e[i]); };

	Section("Engine ports, kWhole: the views' offsets and the thiscall / stdcall calls, run natively");
	{
		const std::uint32_t r = en::ApplyConstants(&map, 0xA1, g, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB,
			EntryFilter::kWhole, NativeGetExtraData, &nameSlot);
		const std::vector<NativeCall> want{
			{kNativeLookup, g, {H(kName)}},
			{0x54, m, {0xA1, E(0), g, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA}},
			{kNativeVertex, dev, {8, H(dataV), 2}},
			{kNativePixel, dev, {3, H(dataP), 1}},
			{0x58, m, {0xA1, E(3), 0xAA}},
			{0x5C, m, {0xA1, E(4), g, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, extra}},
			{0x60, m, {0xA1, E(5), 0xAA}},
			{0x64, m, {0xA1, E(6), g, 0xA3, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA}},
			{0x6C, m, {0xA1, E(7), g, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA}},
			{0x54, m, {0xA1, E(8), g, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA}},
		};
		Check(NativeLog() == want,
			"GetExtraData(ECX = a2, *[name slot]); every slot through [[map]+n] with ECX = map; uploads through "
			"[[map+0x28]]+0x178 / +0x1B4 with [entry+0x1C], [entry+0x30], [entry+0x20]");
		Check(map.lastProgram == 0xA1u, "[map+0x20] = a1");
		Check(r == 1, "the object slot's 1 returns 1");
	}

	Section("Engine ports, kView: [entry+0x10] read through the view; only defined view ids, no lookup");
	{
		NativeLog().clear();
		map.lastProgram = 0;
		const std::uint32_t r = en::ApplyConstants(&map, 0xB1, g, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB,
			EntryFilter::kView, NativeGetExtraData, &nameSlot);
		const std::vector<NativeCall> want{{0x54, m, {0xB1, E(0), g, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA}}};
		Check(NativeLog() == want, "slot +0x54 for id 19 only");
		Check(map.lastProgram == 0xB1u && r == 0, "[map+0x20] = a1; returns 0");
	}

	Section("Engine ports: u16 count 0 at +0x16 with a garbage +0x14 - no entry read");
	{
		NativeLog().clear();
		map.entryCount = 0;
		map.unread14 = 0xFFFF;
		const std::uint32_t r = en::ApplyConstants(&map, 0xC1, g, 0, 0, 0, 0, 0, 0, 0, 0, 0, EntryFilter::kWhole,
			NativeGetExtraData, &nameSlot);
		Check(r == 0 && NativeLog().size() == 1 && NativeLog()[0].kind == kNativeLookup, "only the lookup; returns 0");
	}
}

} // namespace

int main() {
	PureDecisions();
	WholeFlows();
	ViewFlows();
	EmulatorGate();
	NativeEnginePorts();
	return Failures();
}
