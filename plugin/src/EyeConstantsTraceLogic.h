#pragma once

// The eye constants trace's decisions ([Route] StereoTraceConstants,
// RouteStereoDraw.cpp), free of the engine
// (plugin/tests/EyeConstantsTraceLogicTest.cpp).
//
// The question: which vertex shader constants the device holds for the
// second eye's draw that differ from the first eye's - per shader class. A
// class whose draws reach the second eye with every register unchanged is
// drawn there with the first eye's view: no depth between the eyes (the
// near ground in the single pass, measured in the eye pictures of run
// obng-grass-turn-single/20261106-082525). The device's registers are read
// back before the second eye's switch, after it and after the switch back.

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::plugin::eye_trace {

constexpr int kRegisters = 48;  // c0..c47: every register the vanilla vertex shaders' view-dependent constants use lies below

// Bit r set: register r (four floats) differs between the two snapshots.
// The floats are compared as bits, so a NaN equals itself.
inline std::uint64_t ChangedRegisters(const float* a, const float* b, int registers = kRegisters) {
	std::uint64_t mask = 0;
	const auto* x = reinterpret_cast<const unsigned char*>(a);
	const auto* y = reinterpret_cast<const unsigned char*>(b);
	for (int r = 0; r < registers && r < 64; ++r) {
		for (int i = 0; i < 16; ++i) {
			if (x[r * 16 + i] != y[r * 16 + i]) {
				mask |= std::uint64_t{1} << r;
				break;
			}
		}
	}
	return mask;
}

// "c9-c12 c15" for a mask, "none" for an empty one.
inline int FormatRegisters(std::uint64_t mask, char* out, std::size_t capacity) {
	if (mask == 0) {
		return std::snprintf(out, capacity, "none");
	}
	int used = 0;
	for (int r = 0; r < 64; ++r) {
		if ((mask >> r & 1u) == 0) {
			continue;
		}
		int last = r;
		while (last + 1 < 64 && (mask >> (last + 1) & 1u) != 0) {
			++last;
		}
		const int n = last == r ? std::snprintf(out + used, capacity - static_cast<std::size_t>(used), "%sc%d", used != 0 ? " " : "", r)
		                        : std::snprintf(out + used, capacity - static_cast<std::size_t>(used), "%sc%d-c%d", used != 0 ? " " : "", r, last);
		if (n < 0 || static_cast<std::size_t>(used + n) >= capacity) {
			break;
		}
		used += n;
		r = last;
	}
	return used;
}

// Which traces are written, per shader class and pass (a key the caller
// makes of the two): the first of a class always, and after it only those that say something new - no
// register changed for the second eye, or the first eye's registers not
// back - up to a few each, so a class that is wrong shows more than once
// and a class that is right stays one line.
struct ClassBook {
	static constexpr std::uint32_t kMaxClasses = 256;
	static constexpr std::uint32_t kFaultLines = 3;
	struct Row {
		std::uint32_t key = 0;
		std::uint32_t traced = 0;
		std::uint32_t unchanged = 0;    // traces with no register changed for the second eye
		std::uint32_t notRestored = 0;  // traces whose first-eye registers were not back
	};
	Row rows[kMaxClasses] = {};
	std::uint32_t size = 0;

	Row* Find(std::uint32_t key) {
		for (std::uint32_t i = 0; i < size; ++i) {
			if (rows[i].key == key) {
				return &rows[i];
			}
		}
		return nullptr;
	}

	// Counts the trace and says whether its line is written. A class beyond
	// the table's room is neither counted nor written.
	bool Note(std::uint32_t key, bool anyChanged, bool restored) {
		Row* row = Find(key);
		const bool first = row == nullptr;
		if (first) {
			if (size == kMaxClasses) {
				return false;
			}
			row = &rows[size++];
			row->key = key;
		}
		++row->traced;
		bool write = first;
		if (!anyChanged && row->unchanged++ < kFaultLines) {
			write = true;
		}
		if (!restored && row->notRestored++ < kFaultLines) {
			write = true;
		}
		return write;
	}
};

}  // namespace obng::plugin::eye_trace
