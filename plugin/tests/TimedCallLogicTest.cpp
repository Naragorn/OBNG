// Every flow of the timed calls' bookkeeping, INI line and thunk bytes
// (plugin/src/TimedCallLogic.h).

#include "Check.h"
#include "TimedCallLogic.h"

#include <string>
#include <vector>

using namespace obng::plugin::timed;

namespace {

constexpr std::uint32_t kStub = 0x70000000;

void TestCalls() {
	Section("Enter and Exit: one call, its ticks, the real return address back");
	{
		Calls c;
		Check(c.Enter(3, 0x1000, 0x00401234, kStub, 100), "the slot is to be replaced");
		Check(c.Depth() == 1, "one frame");
		// The function pops 8 bytes of arguments: the stack pointer after its RET is slot + 4 + 8.
		Check(c.Exit(0x1000 + 12, 160) == 0x00401234, "the real return address");
		Check(c.Depth() == 0 && c.Of(3).calls == 1 && c.Of(3).inclusiveTicks == 60 && c.Of(3).exclusiveTicks == 60 && c.Of(3).longestTicks == 60, "60 ticks, all its own");
	}

	Section("Nested calls: the inner one's ticks leave the outer one's own time");
	{
		Calls c;
		c.Enter(0, 0x2000, 0xA, kStub, 0);
		c.Enter(1, 0x1F00, 0xB, kStub, 10);
		c.Enter(1, 0x1E00, 0xC, kStub, 20);  // recursion
		Check(c.Exit(0x1E04, 25) == 0xC, "innermost first");
		Check(c.Exit(0x1F04, 50) == 0xB, "then the one around it");
		c.Enter(2, 0x1F00, 0xD, kStub, 60);  // another call at the slot the first inner one had
		Check(c.Exit(0x1F04, 70) == 0xD, "a slot reused after a return is a new frame");
		Check(c.Exit(0x2004, 100) == 0xA, "the outer one last");
		Check(c.Of(1).calls == 2 && c.Of(1).inclusiveTicks == 45 && c.Of(1).exclusiveTicks == 40 && c.Of(1).longestTicks == 40, "function 1: 5 + 40 in all, 5 + 35 its own");
		Check(c.Of(0).inclusiveTicks == 100 && c.Of(0).exclusiveTicks == 50, "function 0: 100 in all, 40 and 10 of it inside");
		Check(c.Of(2).calls == 1 && c.Of(2).inclusiveTicks == 10, "function 2");
	}

	Section("A call left by an exception: its frame is dropped at the next call or return");
	{
		Calls c;
		c.Enter(0, 0x3000, 0xA, kStub, 0);
		c.Enter(1, 0x2F00, 0xB, kStub, 10);
		c.Enter(2, 0x2E00, 0xC, kStub, 20);
		// 1 and 2 are unwound; 0 calls again at a slot between them.
		Check(c.Enter(3, 0x2E80, 0xD, kStub, 30), "the new call is timed");
		Check(c.Depth() == 3 && c.Of(2).dropped == 1 && c.Of(1).dropped == 0, "the frame below the new slot is dropped, the one above it is not known to be gone yet");
		Check(c.Exit(0x2E84, 40) == 0xD, "the new call returns");
		// 0 returns: 1's frame is still there, below the stack pointer.
		Check(c.Exit(0x3004, 90) == 0xA, "the outer call's return address, not the abandoned one's");
		Check(c.Of(1).dropped == 1 && c.Of(1).calls == 0 && c.Depth() == 0, "the abandoned frame is dropped, not counted as a call");
		Check(c.Of(0).calls == 1 && c.Of(0).inclusiveTicks == 90 && c.Of(0).exclusiveTicks == 80, "the outer one keeps its time; the call that finished inside a dropped frame still counts as inside");
	}
	{
		Calls c;
		c.Enter(1, 0x2F00, 0xB, kStub, 0);
		Check(c.Enter(2, 0x2F00, 0xC, kStub, 5), "a new call at the very slot of a frame that never returned");
		Check(c.Depth() == 1 && c.Of(1).dropped == 1, "replaces it");
		Check(c.Exit(0x2F04, 9) == 0xC, "and returns to its own caller");
	}

	Section("A tail call: the slot holds the stub already, the frame's real return address is taken over");
	{
		Calls c;
		c.Enter(0, 0x4000, 0xAAAA, kStub, 0);
		Check(!c.Enter(1, 0x4000, kStub, kStub, 30), "the slot is not replaced again");
		Check(c.Depth() == 1 && c.Of(0).calls == 1 && c.Of(0).inclusiveTicks == 30, "the jumping function is closed at the jump");
		Check(c.Exit(0x4004, 50) == 0xAAAA, "the jumped-to function returns to the first one's caller");
		Check(c.Of(1).calls == 1 && c.Of(1).inclusiveTicks == 20, "with its own ticks");
	}
	{
		Calls c;
		c.Enter(0, 0x4000, 0xAAAA, kStub, 0);
		Check(!c.Enter(1, 0x3F00, kStub, kStub, 10), "the stub in a slot no frame owns: untimed");
		Check(c.Depth() == 1 && c.Of(1).refused == 1, "nothing noted for it, the outer frame stays");
		Check(c.Exit(0x4004, 20) == 0xAAAA, "the outer call still returns");
	}

	Section("Refusals: the frames full, an index out of range, a return without a frame");
	{
		Calls c;
		for (std::size_t i = 0; i < kDepthAtMost; ++i) {
			Check(c.Enter(0, 0x80000 - 16 * static_cast<std::uint32_t>(i), 0x100 + static_cast<std::uint32_t>(i), kStub, i), "room");
		}
		Check(!c.Enter(1, 0x1000, 0xB, kStub, 500) && c.Of(1).refused == 1 && c.Depth() == kDepthAtMost, "full: untimed, the slot stays");
		Check(!c.Enter(static_cast<std::uint32_t>(kFunctionsAtMost), 0x900, 0xB, kStub, 500), "an index beyond the table: untimed");
		Check(c.Exit(0x80000 - 16 * static_cast<std::uint32_t>(kDepthAtMost - 1) + 4, 600) == 0x100 + kDepthAtMost - 1, "the innermost frame returns");
		Calls empty;
		Check(empty.Exit(0x5000, 1) == 0, "no frame: 0");
		Calls above;
		above.Enter(0, 0x6000, 0xA, kStub, 0);
		Check(above.Exit(0x6000, 1) == 0 && above.Depth() == 1, "a stack pointer not above any frame's slot: 0, nothing dropped");
	}
}

void TestWindowAndFormat() {
	Section("Window and the line");
	Totals before;
	before.calls = 10;
	before.inclusiveTicks = 1000;
	before.exclusiveTicks = 600;
	Totals now = before;
	now.calls = 110;
	now.inclusiveTicks = 31000;
	now.exclusiveTicks = 15600;
	now.longestTicks = 900;
	const Totals w = Window(now, before);
	Check(w.calls == 100 && w.inclusiveTicks == 30000 && w.exclusiveTicks == 15000 && w.longestTicks == 900 && w.dropped == 0 && w.refused == 0, "the differences, the longest as read");
	char out[300];
	FormatTimed("Actor::Update", 0x00603CA0, w, 3000.0, out, sizeof(out));
	Check(std::string(out) == "Timed Actor::Update 0x00603CA0 - 100 calls, 10.000 ms in all (100.0 us each), 5.000 ms without the timed functions inside, longest 0.300 ms",
	      (std::string("line: ") + out).c_str());
	Totals odd;
	odd.dropped = 2;
	odd.refused = 3;
	FormatTimed("x", 1, odd, 0.0, out, sizeof(out));
	Check(std::string(out) == "Timed x 0x00000001 - 0 calls, 0.000 ms in all (0.0 us each), 0.000 ms without the timed functions inside, longest 0.000 ms, 2 dropped, 3 untimed",
	      (std::string("no calls, no clock: ") + out).c_str());
}

void TestSpec() {
	Section("The INI line");
	Spec s;
	Check(ParseSpec("00603CA0 6AFF68E00D9D00 Actor::Update", &s), "address, bytes, name");
	const std::uint8_t want[] = {0x6A, 0xFF, 0x68, 0xE0, 0x0D, 0x9D, 0x00};
	Check(s.address == 0x00603CA0 && s.length == 7 && std::memcmp(s.bytes, want, 7) == 0 && std::string(s.name) == "Actor::Update", "all three");
	Check(ParseSpec("  603ca0\t558bec83e4f0   the update of an actor  \r\n", &s) && s.address == 0x603CA0 && s.length == 6 && std::string(s.name) == "the update of an actor",
	      "lower case, tabs, a name with spaces, the line's end cut");
	Check(ParseSpec("00603CA0 558BEC83E4F0", &s) && std::string(s.name) == "0x00603CA0", "no name: the address stands in");
	Check(!ParseSpec("", &s) && !ParseSpec("   ", &s) && !ParseSpec("Actor 558BEC83E4", &s), "no address");
	Check(!ParseSpec("00603CA0", &s) && !ParseSpec("00603CA0 ", &s), "no bytes");
	Check(!ParseSpec("00603CA0 558BEC83", &s), "four bytes: too short for the jump");
	Check(!ParseSpec("00603CA0 558BEC83E4F", &s), "half a byte");
	Check(!ParseSpec("00603CA0 558BEC83E4F0x name", &s), "something glued to the bytes");
	Check(!ParseSpec("000603CA00 558BEC83E4F0", &s), "an address of nine digits");
	Check(ParseSpec("1 000102030405060708090A0B0C0D0E n", &s) && s.length == 15, "fifteen bytes fit");
	Check(!ParseSpec("1 000102030405060708090A0B0C0D0E0F n", &s), "sixteen do not");
	const std::string longName(80, 'n');
	Check(ParseSpec(("1 0001020304 " + longName).c_str(), &s) && std::string(s.name).size() == kNameAtMost, "a long name is cut");
}

void TestThunks() {
	Section("The entry thunk's bytes");
	std::uint8_t code[40];
	const std::uint32_t n = BuildEntryThunk(code, sizeof(code), 0x10000000, 7, 0x10001000, 0x20000000);
	const std::vector<std::uint8_t> want = {0x50, 0x51, 0x52, 0x8D, 0x44, 0x24, 0x0C, 0x50, 0x68, 0x07, 0x00, 0x00, 0x00,
	                                        0xE8, 0xEE, 0x0F, 0x00, 0x00,                    // call: 0x10001000 - 0x10000012
	                                        0x8D, 0x64, 0x24, 0x08, 0x5A, 0x59, 0x58,
	                                        0xE9, 0xE2, 0xFF, 0xFF, 0x0F};                   // jmp: 0x20000000 - 0x1000001E
	Check(n == kEntryThunkSize && n == want.size() && std::memcmp(code, want.data(), n) == 0, "push the registers, the slot's address and the index, call, restore, jump on");
	Check(BuildEntryThunk(code, kEntryThunkSize - 1, 0, 0, 0, 0) == 0, "too small a buffer: nothing");

	Section("The exit stub's bytes");
	const std::uint32_t m = BuildExitStub(code, sizeof(code), 0x10000100, 0x10002000);
	const std::vector<std::uint8_t> exitWant = {0x8D, 0x64, 0x24, 0xFC, 0x50, 0x51, 0x52, 0x8D, 0x44, 0x24, 0x0C, 0x50,
	                                            0xE8, 0xEF, 0x1E, 0x00, 0x00,                // call: 0x10002000 - 0x10000111
	                                            0x8D, 0x64, 0x24, 0x04, 0x5A, 0x59, 0x58, 0xC3};
	Check(m == kExitStubSize, "the size as declared");
	Check(m == exitWant.size() && std::memcmp(code, exitWant.data(), m) == 0, "room, the registers, the room's address, call, restore, ret");
	Check(BuildExitStub(code, 8, 0, 0) == 0, "too small a buffer: nothing");
}

}  // namespace

void TestSpikes() {
	Section("A watched function's long call: what ran inside it");
	{
		Calls c;
		Spike spike;
		c.Watch(0, 100);
		Check(!c.TakeSpike(&spike), "nothing yet");
		// A short call of the watched function: no spike.
		c.Enter(0, 0x9000, 0xA, kStub, 0);
		c.Enter(1, 0x8000, 0xB, kStub, 10);
		c.Exit(0x8004, 30);
		c.Exit(0x9004, 60);
		Check(!c.TakeSpike(&spike), "60 ticks against a threshold of 100: none");
		// A long one: function 1 twice (20 and 150 ticks), function 2 once inside the second.
		c.Enter(0, 0x9000, 0xA, kStub, 1000);
		c.Enter(1, 0x8000, 0xB, kStub, 1010);
		c.Exit(0x8004, 1030);
		c.Enter(1, 0x8000, 0xB, kStub, 1040);
		c.Enter(2, 0x7000, 0xC, kStub, 1050);
		c.Exit(0x7004, 1150);
		c.Exit(0x8004, 1190);
		c.Exit(0x9004, 1200);
		Check(c.TakeSpike(&spike), "200 ticks: a spike");
		Check(spike.root == 0 && spike.rootTicks == 200, "the watched function and its ticks");
		Check(spike.of[1].calls == 2 && spike.of[1].ticks == 170 && spike.of[1].longestTicks == 150, "function 1: two calls, 170 ticks, the longest 150");
		Check(spike.of[2].calls == 1 && spike.of[2].ticks == 100 && spike.of[2].longestTicks == 100, "function 2: one call of 100");
		Check(spike.of[0].calls == 1 && spike.of[0].ticks == 200, "the watched function itself counts once");
		Check(spike.of[3].calls == 0 && spike.of[3].ticks == 0, "a function not called inside: nothing");
		Check(!c.TakeSpike(&spike), "taken once: gone");
	}

	Section("What ran before the watched call does not count; the longest of two spikes not taken is kept");
	{
		Calls c;
		Spike spike;
		c.Watch(0, 100);
		c.Enter(1, 0x8000, 0xB, kStub, 0);
		c.Exit(0x8004, 500);  // outside the watched function
		c.Enter(0, 0x9000, 0xA, kStub, 1000);
		c.Exit(0x9004, 1150);
		c.Enter(0, 0x9000, 0xA, kStub, 2000);
		c.Enter(1, 0x8000, 0xB, kStub, 2010);
		c.Exit(0x8004, 2020);
		c.Exit(0x9004, 2400);
		c.Enter(0, 0x9000, 0xA, kStub, 3000);
		c.Exit(0x9004, 3120);
		Check(c.SpikesSeen() == 3, "three long calls seen");
		Check(c.TakeSpike(&spike) && spike.rootTicks == 400 && spike.of[1].calls == 1 && spike.of[1].ticks == 10, "the longest waits, with its own inside counts only");
	}

	Section("The watched function inside itself, dropped, or not watched at all");
	{
		Calls c;
		Spike spike;
		c.Watch(0, 100);
		c.Enter(0, 0x9000, 0xA, kStub, 0);
		c.Enter(0, 0x8000, 0xB, kStub, 10);  // recursion: the outer call is the watched one
		c.Exit(0x8004, 160);
		Check(!c.TakeSpike(&spike), "the inner call's return ends nothing");
		c.Exit(0x9004, 300);
		Check(c.TakeSpike(&spike) && spike.rootTicks == 300 && spike.of[0].calls == 2, "the outer call: 300 ticks, the function counted twice");
		// The watched frame is dropped (an exception passed through): no spike, the next call is watched afresh.
		c.Enter(0, 0x9000, 0xA, kStub, 1000);
		c.Enter(1, 0x9800, 0xB, kStub, 2000);  // a call at a higher slot: the frame at 0x9000 is gone
		c.Exit(0x9804, 2010);
		Check(!c.TakeSpike(&spike), "a dropped watched call gives no spike");
		c.Enter(0, 0x9000, 0xA, kStub, 3000);
		c.Exit(0x9004, 3200);
		Check(c.TakeSpike(&spike) && spike.rootTicks == 200 && spike.of[1].calls == 0, "and the next one starts clean");
		Calls plain;
		plain.Enter(0, 0x9000, 0xA, kStub, 0);
		plain.Exit(0x9004, 100000);
		Check(!plain.TakeSpike(&spike) && plain.SpikesSeen() == 0, "nothing watched: never a spike");
		c.Watch(kFunctionsAtMost, 1);
		c.Enter(0, 0x9000, 0xA, kStub, 5000);
		c.Exit(0x9004, 9000);
		Check(!c.TakeSpike(&spike), "an index out of range watches nothing");
	}

	Section("The spike's line");
	{
		Spike spike;
		spike.root = 0;
		spike.rootTicks = 20000;
		spike.of[0] = {1, 20000, 20000};
		spike.of[1] = {25, 1500, 200};
		spike.of[2] = {1, 15000, 15000};
		spike.of[3] = {4, 40, 20};
		const char* names[4] = {"actors:ProcessManager", "actors:ApplyAnimData", "actors:HighUpdate", "tiny"};
		char line[512];
		FormatSpike(spike, names, 4, 1000.0, 0.05, line, sizeof(line));
		std::printf("    %s\n", line);
		const std::string text(line);
		Check(text.find("Timed spike: actors:ProcessManager 20.000 ms") == 0, "the watched function first");
		const std::size_t high = text.find("actors:HighUpdate 15.000 ms (1 call, longest 15.000)");
		const std::size_t anim = text.find("actors:ApplyAnimData 1.500 ms (25 calls, longest 0.200)");
		Check(high != std::string::npos && anim != std::string::npos && high < anim, "then the functions inside by their time, the largest first");
		Check(text.find("tiny") == std::string::npos, "one below the least share is left out");
		char small[40];
		FormatSpike(spike, names, 4, 1000.0, 0.1, small, sizeof(small));
		Check(std::strlen(small) < sizeof(small), "a small buffer is not overrun");
	}
}

int main() {
	TestCalls();
	TestSpikes();
	TestWindowAndFormat();
	TestSpec();
	TestThunks();
	return Failures();
}
