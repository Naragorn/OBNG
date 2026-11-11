#pragma once

// Any function of the EXE timed at its entry and its return, whatever its
// calling convention - the bookkeeping, the thunks' bytes and the INI line,
// over plain values ([Timed] in OBNG.ini; TimedCalls.cpp installs it).
//
// How: the function's entry jumps to a thunk that hands the address of the
// return address on the stack to Enter and continues in the original. Enter
// notes the return address and puts the exit stub's address in its place, so
// the function returns into the stub - with its own RET, popping its own
// arguments, which is why the convention does not matter. The stub asks Exit
// for the real return address and returns there. Between the two lie the
// function's ticks.
//
// A function left by an exception or a longjmp never reaches the stub. Its
// frame is recognised by the stack pointer: the stack grows downwards, so a
// frame noted at a slot at or below the slot of a new call, or below the
// stack pointer of a return, belongs to a call that is gone, and is dropped.
//
// One thread only (the caller's choice; TimedCalls.cpp takes the first
// thread that calls the first listed function): the frames are a plain array.
//
// 64-bit clean: addresses and stack pointers are std::uint32_t handles.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace obng::plugin::timed {

constexpr std::size_t kFunctionsAtMost = 48;
constexpr std::size_t kDepthAtMost = 128;

struct Totals {
	std::uint64_t calls = 0;
	std::uint64_t inclusiveTicks = 0;
	std::uint64_t exclusiveTicks = 0;  // without the timed functions called inside
	std::uint64_t longestTicks = 0;
	std::uint64_t dropped = 0;  // frames dropped as gone (an exception passed through)
	std::uint64_t refused = 0;  // calls not timed: the frames were full
};

class Calls {
public:
	// A timed function was entered: `slot` is where its return address lies,
	// `returnAddress` what lies there, `stub` the exit stub's address. True:
	// the caller must put the stub's address into the slot. False: the call
	// runs untimed and the slot stays as it is (the frames are full; or the
	// slot holds the stub already and no frame of ours owns it).
	//
	// The slot holding the stub already is a tail call: a timed function
	// ended in a jump to this one, which will return for it. That frame is
	// closed here and this one takes its real return address over.
	bool Enter(std::uint32_t index, std::uint32_t slot, std::uint32_t returnAddress, std::uint32_t stub, std::uint64_t now) {
		if (index >= kFunctionsAtMost) {
			return false;
		}
		if (returnAddress == stub) {
			DropBelow(slot);
			if (depth_ == 0 || frames_[depth_ - 1].slot != slot) {
				++totals_[index].refused;
				return false;
			}
			const std::uint32_t real = frames_[depth_ - 1].returnAddress;
			Close(now);
			frames_[depth_++] = Frame{index, slot, real, now, 0};
			return false;  // the stub is in the slot already
		}
		DropBelow(slot + 1);  // a frame at this slot or below it is gone
		if (depth_ >= kDepthAtMost) {
			++totals_[index].refused;
			return false;
		}
		frames_[depth_++] = Frame{index, slot, returnAddress, now, 0};
		return true;
	}

	// The exit stub was reached with the stack pointer `stackPointer` (as it
	// was when the function's RET had run: above the returning call's slot
	// and its arguments). Returns the real return address; 0 when no frame
	// fits (which the stub cannot survive - it never happens while every
	// replaced slot has its frame).
	std::uint32_t Exit(std::uint32_t stackPointer, std::uint64_t now) {
		// The returning frame is the outermost one below the stack pointer;
		// the ones above it in the array (deeper calls) are gone.
		std::size_t at = depth_;
		while (at > 0 && frames_[at - 1].slot < stackPointer) {
			--at;
		}
		if (at == depth_) {
			return 0;
		}
		while (depth_ > at + 1) {
			Drop();
		}
		const std::uint32_t real = frames_[depth_ - 1].returnAddress;
		Close(now);
		return real;
	}

	const Totals& Of(std::size_t index) const { return totals_[index]; }
	std::size_t Depth() const { return depth_; }

private:
	struct Frame {
		std::uint32_t index;
		std::uint32_t slot;
		std::uint32_t returnAddress;
		std::uint64_t start;
		std::uint64_t childTicks;
	};

	// The top frame ends at `now`: its totals, and its time into the frame
	// around it.
	void Close(std::uint64_t now) {
		const Frame frame = frames_[--depth_];
		const std::uint64_t inclusive = now >= frame.start ? now - frame.start : 0;
		Totals& t = totals_[frame.index];
		++t.calls;
		t.inclusiveTicks += inclusive;
		t.exclusiveTicks += inclusive >= frame.childTicks ? inclusive - frame.childTicks : 0;
		if (inclusive > t.longestTicks) {
			t.longestTicks = inclusive;
		}
		if (depth_ > 0) {
			frames_[depth_ - 1].childTicks += inclusive;
		}
	}

	void DropBelow(std::uint32_t limit) {
		while (depth_ > 0 && frames_[depth_ - 1].slot < limit) {
			Drop();
		}
	}

	// The top frame is gone without a return: not a call, but the timed
	// calls that finished inside it stay known to the frame around it.
	void Drop() {
		const Frame frame = frames_[--depth_];
		++totals_[frame.index].dropped;
		if (depth_ > 0) {
			frames_[depth_ - 1].childTicks += frame.childTicks;
		}
	}

	Frame frames_[kDepthAtMost] = {};
	std::size_t depth_ = 0;
	Totals totals_[kFunctionsAtMost] = {};
};

// The difference of two readings of a function's totals (the summary's
// window); the longest is the later reading's, which the caller resets by
// keeping its own.
inline Totals Window(const Totals& now, const Totals& before) {
	Totals w;
	w.calls = now.calls - before.calls;
	w.inclusiveTicks = now.inclusiveTicks - before.inclusiveTicks;
	w.exclusiveTicks = now.exclusiveTicks - before.exclusiveTicks;
	w.longestTicks = now.longestTicks;
	w.dropped = now.dropped - before.dropped;
	w.refused = now.refused - before.refused;
	return w;
}

// "Timed <name> 0xAAAAAAAA - N calls, N.NNN ms in all (N.N us each), N.NNN
// ms without the timed functions inside, longest N.NNN ms[, N dropped][, N
// untimed]".
inline int FormatTimed(const char* name, std::uint32_t address, const Totals& w, double ticksPerMs, char* out, std::size_t capacity) {
	const double toMs = ticksPerMs > 0.0 ? 1.0 / ticksPerMs : 0.0;
	const double inclusive = static_cast<double>(w.inclusiveTicks) * toMs;
	int at = std::snprintf(out, capacity, "Timed %s 0x%08X - %llu calls, %.3f ms in all (%.1f us each), %.3f ms without the timed functions inside, longest %.3f ms",
	                       name, address, static_cast<unsigned long long>(w.calls), inclusive,
	                       w.calls != 0 ? inclusive * 1000.0 / static_cast<double>(w.calls) : 0.0, static_cast<double>(w.exclusiveTicks) * toMs,
	                       static_cast<double>(w.longestTicks) * toMs);
	if (w.dropped != 0 && at > 0 && static_cast<std::size_t>(at) < capacity) {
		at += std::snprintf(out + at, capacity - at, ", %llu dropped", static_cast<unsigned long long>(w.dropped));
	}
	if (w.refused != 0 && at > 0 && static_cast<std::size_t>(at) < capacity) {
		at += std::snprintf(out + at, capacity - at, ", %llu untimed", static_cast<unsigned long long>(w.refused));
	}
	return at;
}

// ---- The INI line ---------------------------------------------------------------------------
//
// "<address hex> <entry bytes hex> <name...>", e.g.
// "00603CA0 6AFF68E00D9D00 Actor::Update". The bytes are the whole
// instructions the five-byte patch displaces, as the EXE has them (checked
// against the EXE at install); they must hold nothing relative.

constexpr std::size_t kEntryBytesAtMost = 15;
constexpr std::size_t kNameAtMost = 47;

struct Spec {
	std::uint32_t address = 0;
	std::uint8_t bytes[kEntryBytesAtMost] = {};
	std::uint32_t length = 0;
	char name[kNameAtMost + 1] = {};
};

inline int HexDigit(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

inline bool ParseSpec(const char* text, Spec* out) {
	*out = Spec{};
	const char* p = text;
	while (*p == ' ' || *p == '\t') {
		++p;
	}
	unsigned digits = 0;
	std::uint32_t address = 0;
	for (; HexDigit(*p) >= 0; ++p, ++digits) {
		if (digits >= 8) {
			return false;
		}
		address = (address << 4) | static_cast<std::uint32_t>(HexDigit(*p));
	}
	if (digits == 0 || (*p != ' ' && *p != '\t')) {
		return false;
	}
	while (*p == ' ' || *p == '\t') {
		++p;
	}
	std::uint32_t length = 0;
	while (HexDigit(p[0]) >= 0) {
		if (HexDigit(p[1]) < 0 || length >= kEntryBytesAtMost) {
			return false;  // half a byte, or more bytes than an entry may displace
		}
		out->bytes[length++] = static_cast<std::uint8_t>((HexDigit(p[0]) << 4) | HexDigit(p[1]));
		p += 2;
	}
	if (length < 5 || (*p != '\0' && *p != ' ' && *p != '\t')) {
		return false;  // the patch is a five-byte jump
	}
	while (*p == ' ' || *p == '\t') {
		++p;
	}
	std::size_t n = 0;
	for (; *p != '\0' && *p != '\r' && *p != '\n' && n < kNameAtMost; ++p) {
		out->name[n++] = *p;
	}
	while (n > 0 && (out->name[n - 1] == ' ' || out->name[n - 1] == '\t')) {
		--n;
	}
	out->name[n] = '\0';
	if (n == 0) {
		std::snprintf(out->name, sizeof(out->name), "0x%08X", address);
	}
	out->address = address;
	out->length = length;
	return true;
}

// ---- The thunks' bytes -----------------------------------------------------------------------

constexpr std::uint32_t kEntryThunkSize = 30;
constexpr std::uint32_t kExitStubSize = 25;

namespace detail {
inline void Put32(std::uint8_t* at, std::uint32_t value) {
	std::memcpy(at, &value, 4);
}
}  // namespace detail

// The entry thunk of function `index`, to execute at `base`:
//   push eax / push ecx / push edx     the registers a convention may pass in
//   lea eax,[esp+0Ch]                  where the return address lies
//   push eax / push index
//   call enter                         void __cdecl(std::uint32_t index, std::uint32_t* slot)
//   lea esp,[esp+8]
//   pop edx / pop ecx / pop eax
//   jmp original                       the displaced entry and on
// Returns the bytes written, 0 when the capacity is too small.
inline std::uint32_t BuildEntryThunk(std::uint8_t* buffer, std::uint32_t capacity, std::uint32_t base, std::uint32_t index, std::uint32_t enter,
                                     std::uint32_t original) {
	if (capacity < kEntryThunkSize) {
		return 0;
	}
	std::uint8_t* p = buffer;
	*p++ = 0x50;
	*p++ = 0x51;
	*p++ = 0x52;
	*p++ = 0x8D;
	*p++ = 0x44;
	*p++ = 0x24;
	*p++ = 0x0C;
	*p++ = 0x50;
	*p++ = 0x68;
	detail::Put32(p, index);
	p += 4;
	*p++ = 0xE8;
	detail::Put32(p, enter - (base + static_cast<std::uint32_t>(p - buffer) + 4));
	p += 4;
	*p++ = 0x8D;
	*p++ = 0x64;
	*p++ = 0x24;
	*p++ = 0x08;
	*p++ = 0x5A;
	*p++ = 0x59;
	*p++ = 0x58;
	*p++ = 0xE9;
	detail::Put32(p, original - (base + static_cast<std::uint32_t>(p - buffer) + 4));
	p += 4;
	return static_cast<std::uint32_t>(p - buffer);
}

// The exit stub every timed function returns into, to execute at `base`:
//   lea esp,[esp-4]                    room for the real return address
//   push eax / push ecx / push edx     the result's registers
//   lea eax,[esp+0Ch]                  that room
//   push eax
//   call exit                          void __cdecl(std::uint32_t* room): fills it
//   lea esp,[esp+4]
//   pop edx / pop ecx / pop eax
//   ret
// The flags and the x87 stack (a float result) are not touched: LEA instead
// of ADD/SUB, and `exit` is integer code.
inline std::uint32_t BuildExitStub(std::uint8_t* buffer, std::uint32_t capacity, std::uint32_t base, std::uint32_t exit) {
	if (capacity < kExitStubSize) {
		return 0;
	}
	std::uint8_t* p = buffer;
	*p++ = 0x8D;
	*p++ = 0x64;
	*p++ = 0x24;
	*p++ = 0xFC;
	*p++ = 0x50;
	*p++ = 0x51;
	*p++ = 0x52;
	*p++ = 0x8D;
	*p++ = 0x44;
	*p++ = 0x24;
	*p++ = 0x0C;
	*p++ = 0x50;
	*p++ = 0xE8;
	detail::Put32(p, exit - (base + static_cast<std::uint32_t>(p - buffer) + 4));
	p += 4;
	*p++ = 0x8D;
	*p++ = 0x64;
	*p++ = 0x24;
	*p++ = 0x04;
	*p++ = 0x5A;
	*p++ = 0x59;
	*p++ = 0x58;
	*p++ = 0xC3;
	return static_cast<std::uint32_t>(p - buffer);
}

}  // namespace obng::plugin::timed
