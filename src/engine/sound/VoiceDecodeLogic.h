#pragma once
// A voice line's decode off the main thread - the decisions over plain values
// ([Route] VoiceDecode in the plugin's VoiceDecode.cpp; knowledge/voice-lines.md).
//
// The voice player 0x006A9E00 opens a line's MP3, makes a DirectSound buffer
// for the whole line and decodes every frame into it before it returns: 3-28
// ms on the main thread for each line an actor starts (measured 2026-11-13).
// The route keeps the function's results - the buffer's format and size, the
// duration it returns - and decodes only the line's first frames at the call;
// a worker thread writes the rest while the line plays.
//
// What the original computes, read in its instructions (0x006A9E00-0x006AA15E):
//   the path copied, ".wav" (the first one, case-sensitive) made ".mp3";
//   frames = the file object's count (FF FB byte pairs in the whole file,
//   0x006B4590); the buffer's bytes = frames * 0x900 (one frame is 1152
//   16-bit samples, mono); the flags 0xA0, with 0x20010 more when the call's
//   flags have bit 1; the duration in ms = ((bytes >> 1) / (rate / 1000)),
//   its low 16 bits, returned in AX (0 = failure, the caller then tries the
//   WAV loader).
//
// 64-bit clean: plain values only.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::engine::sound::voice_decode {

constexpr std::uint32_t kFrameSamples = 1152;
constexpr std::uint32_t kFrameBytes = 0x900;
constexpr std::size_t kPathCapacity = 260;  // the original's stack buffer
static_assert(kFrameBytes == kFrameSamples * 2);

enum class Mode : std::uint8_t {
	kOff = 0,            // the game's own function
	kEngineDecoder = 1,  // the game's decoder, the line's rest on the worker
	kOwnDecoder = 2,     // minimp3, the line's rest on the worker
};

// [Route] VoiceDecode: 0, 1 or 2; anything else is the default.
constexpr Mode ModeOf(int value, Mode fallback) {
	return value == 0 ? Mode::kOff : value == 1 ? Mode::kEngineDecoder : value == 2 ? Mode::kOwnDecoder : fallback;
}

// The path as the original makes it: copied, the first ".wav" made ".mp3".
// False when it does not fit the original's buffer with its terminator (the
// original would write past it; the route leaves such a call to it).
inline bool Mp3Path(const char* path, char* out, std::size_t capacity) {
	if (path == nullptr || out == nullptr || capacity == 0) {
		return false;
	}
	std::size_t n = 0;
	for (; path[n] != 0; ++n) {
		if (n + 1 >= capacity) {
			return false;
		}
		out[n] = path[n];
	}
	out[n] = 0;
	for (std::size_t i = 0; i + 4 <= n; ++i) {
		if (out[i] == '.' && out[i + 1] == 'w' && out[i + 2] == 'a' && out[i + 3] == 'v') {
			out[i + 1] = 'm';
			out[i + 2] = 'p';
			out[i + 3] = '3';
			break;
		}
	}
	return true;
}

// The buffer's bytes for a frame count (32-bit, as the original's shift).
constexpr std::uint32_t BufferBytes(std::uint32_t frames) {
	return frames * kFrameBytes;
}

// The most frames a buffer can hold without the byte count wrapping.
constexpr std::uint32_t kFramesAtMost = 0xFFFFFFFFu / kFrameBytes;

// The duration the original returns in AX; 0 for a rate under 1000 (the
// original would divide by zero - its decoder accepts 32000, 44100, 48000).
constexpr std::uint16_t DurationMs(std::uint32_t frames, std::uint32_t rate) {
	const std::uint32_t perMs = rate / 1000;
	return perMs == 0 ? std::uint16_t{0} : static_cast<std::uint16_t>((BufferBytes(frames) >> 1) / perMs);
}

// The DirectSound buffer's capability flags for the call's flags: `always`
// for every line, `positional` beside it for a call with bit 1 (read
// 0x006A9FBC-0x006A9FC8: sbb eax,eax; and eax,20010h; or eax,0A0h). The two
// masks are immediates in the game's code, and other plugins change them -
// seen 2026-11-14: the second byte of the OR's immediate patched to 80h,
// which is DSBCAPS_GLOBALFOCUS (the line goes on when the window loses the
// focus) - so the route reads them there (FlagMasks).
inline constexpr std::uint32_t kFlagsAlways = 0xA0u;
inline constexpr std::uint32_t kFlagsPositional = 0x20010u;
constexpr std::uint32_t BufferFlags(std::uint32_t callFlags, std::uint32_t always = kFlagsAlways, std::uint32_t positional = kFlagsPositional) {
	return ((callFlags & 2u) != 0 ? positional : 0u) | always;
}

// The masks from the ten bytes at 0x006A9FBE (and eax,imm32; or eax,imm32);
// the file's when the opcodes are not those any more.
struct Masks {
	std::uint32_t always = kFlagsAlways;
	std::uint32_t positional = kFlagsPositional;
	bool read = false;
};
constexpr Masks FlagMasks(const std::uint8_t code[10]) {
	Masks m;
	if (code[0] != 0x25 || code[5] != 0x0D) {
		return m;
	}
	auto dword = [&](int at) {
		return static_cast<std::uint32_t>(code[at]) | (static_cast<std::uint32_t>(code[at + 1]) << 8) | (static_cast<std::uint32_t>(code[at + 2]) << 16) |
		       (static_cast<std::uint32_t>(code[at + 3]) << 24);
	};
	m.positional = dword(1);
	m.always = dword(6);
	m.read = true;
	return m;
}

// ---- A line without a voice file: another plugin's stand-in ----------------------------
//
// When neither the MP3 nor the WAV exists the original writes "MP3 file '%s'
// not found" (0x006A9EB7-0x006A9EC9) and returns 0. Elys' Universal Silent
// Voice patches that place with `jmp [slot]` to a stub of its own (read from
// the running game, 2026-11-14):
//   mov ecx,esp; add ecx,64h; mov [pathSlot],ecx   ; the function's path buffer
//   push eax; push ebx; push edx; call routine; pop edx; pop ebx; pop eax
//   jmp [continueSlot]
// Its routine rewrites the path in the buffer (to its silent MP3) and the
// function goes on. The route, which never reaches that place, does the same
// by the stub's own means when the stub is exactly this one: it puts its
// buffer's address into pathSlot and calls the routine.
struct SilentStub {
	bool found = false;
	std::uint32_t pathSlot = 0;      // where the stub stores the path buffer's address
	std::uint32_t routine = 0;       // what it calls
	std::uint32_t continueSlot = 0;  // where the address lies that it jumps on to
};

inline constexpr std::size_t kSilentSiteBytes = 6;
inline constexpr std::size_t kSilentStubBytes = 28;

// The six bytes at the not-found place: `jmp [slot]` gives the slot, anything else 0.
constexpr std::uint32_t SilentJumpSlot(const std::uint8_t site[kSilentSiteBytes]) {
	if (site[0] != 0xFF || site[1] != 0x25) {
		return 0;
	}
	return static_cast<std::uint32_t>(site[2]) | (static_cast<std::uint32_t>(site[3]) << 8) | (static_cast<std::uint32_t>(site[4]) << 16) |
	       (static_cast<std::uint32_t>(site[5]) << 24);
}

// The stub's 28 bytes at address `at`.
constexpr SilentStub ParseSilentStub(const std::uint8_t stub[kSilentStubBytes], std::uint32_t at) {
	SilentStub out;
	const std::uint8_t head[7] = {0x89, 0xE1, 0x83, 0xC1, 0x64, 0x89, 0x0D};
	for (int i = 0; i < 7; ++i) {
		if (stub[i] != head[i]) {
			return out;
		}
	}
	if (stub[11] != 0x50 || stub[12] != 0x53 || stub[13] != 0x52 || stub[14] != 0xE8 || stub[19] != 0x5A || stub[20] != 0x5B || stub[21] != 0x58 ||
	    stub[22] != 0xFF || stub[23] != 0x25) {
		return out;
	}
	auto dword = [&](int i) {
		return static_cast<std::uint32_t>(stub[i]) | (static_cast<std::uint32_t>(stub[i + 1]) << 8) | (static_cast<std::uint32_t>(stub[i + 2]) << 16) |
		       (static_cast<std::uint32_t>(stub[i + 3]) << 24);
	};
	out.pathSlot = dword(7);
	out.routine = at + 19 + dword(15);
	out.continueSlot = dword(24);
	out.found = true;
	return out;
}

// How many of the line's frames are decoded at the call: enough sound for
// `leadMs` (DirectSound plays what lies before its write cursor as it was at
// Play; the worker must not have to write there), two at the least, never
// more than the line has.
constexpr std::uint32_t LeadFrames(std::uint32_t leadMs, std::uint32_t rate, std::uint32_t frames) {
	const std::uint64_t samples = static_cast<std::uint64_t>(leadMs) * rate;
	const std::uint64_t perFrame = static_cast<std::uint64_t>(kFrameSamples) * 1000;
	std::uint64_t lead = (samples + perFrame - 1) / perFrame;
	if (lead < 2) {
		lead = 2;
	}
	return lead > frames ? frames : static_cast<std::uint32_t>(lead);
}

// The worker's next piece: at most `chunk` frames of what is left.
constexpr std::uint32_t NextChunk(std::uint32_t done, std::uint32_t total, std::uint32_t chunk) {
	return done >= total ? 0 : (total - done < chunk ? total - done : chunk);
}

// The original's frame count: FF FB byte pairs at the offsets 0 to size - 2.
inline std::uint32_t CountFrames(const std::uint8_t* data, std::size_t size) {
	std::uint32_t count = 0;
	for (std::size_t i = 0; i + 1 < size; ++i) {
		if (data[i] == 0xFF && data[i + 1] == 0xFB) {
			++count;
		}
	}
	return count;
}

// Interleaved samples of `channels` channels to one channel: the mean.
inline void MixToMono(const std::int16_t* in, std::uint32_t channels, std::uint32_t samples, std::int16_t* out) {
	for (std::uint32_t i = 0; i < samples; ++i) {
		std::int32_t sum = 0;
		for (std::uint32_t c = 0; c < channels; ++c) {
			sum += in[i * channels + c];
		}
		out[i] = channels != 0 ? static_cast<std::int16_t>(sum / static_cast<std::int32_t>(channels)) : std::int16_t{0};
	}
}

// The lines in work: a few slots, each free, claimed by a call that is
// still filling it, waiting for the worker, with the worker, or done and
// waiting for the main thread to free what it holds.
class Slots {
public:
	static constexpr std::uint32_t kCount = 8;
	static constexpr std::uint32_t kNone = 0xFFFFFFFFu;
	enum class State : std::uint8_t { kFree, kClaimed, kQueued, kRunning, kDone };

	// A free slot for a new line, claimed (the worker does not take it yet);
	// kNone when all are taken.
	std::uint32_t Claim() {
		for (std::uint32_t i = 0; i < kCount; ++i) {
			if (state_[i] == State::kFree) {
				state_[i] = State::kClaimed;
				return i;
			}
		}
		return kNone;
	}

	// The claimed slot is filled: the worker may take it.
	void Queue(std::uint32_t slot) {
		if (slot < kCount && state_[slot] == State::kClaimed) {
			state_[slot] = State::kQueued;
			order_[slot] = ++sequence_;
		}
	}

	// The claimed slot is not needed after all (the line was decoded whole at
	// the call, or the call failed): done, for the main thread's cleaning.
	void Drop(std::uint32_t slot) {
		if (slot < kCount && state_[slot] == State::kClaimed) {
			state_[slot] = State::kDone;
		}
	}

	// The worker's next line: the one queued longest; kNone when none waits.
	std::uint32_t Take() {
		std::uint32_t best = kNone;
		for (std::uint32_t i = 0; i < kCount; ++i) {
			if (state_[i] == State::kQueued && (best == kNone || order_[i] < order_[best])) {
				best = i;
			}
		}
		if (best != kNone) {
			state_[best] = State::kRunning;
		}
		return best;
	}

	void Finish(std::uint32_t slot) {
		if (slot < kCount && state_[slot] == State::kRunning) {
			state_[slot] = State::kDone;
		}
	}

	// A done slot for the main thread to clean and free; kNone when none.
	std::uint32_t TakeDone() {
		for (std::uint32_t i = 0; i < kCount; ++i) {
			if (state_[i] == State::kDone) {
				state_[i] = State::kFree;
				return i;
			}
		}
		return kNone;
	}

	State Of(std::uint32_t slot) const { return slot < kCount ? state_[slot] : State::kFree; }

	// Lines whose buffer the worker may still write: queued or running.
	std::uint32_t InWork() const {
		std::uint32_t n = 0;
		for (std::uint32_t i = 0; i < kCount; ++i) {
			n += state_[i] == State::kQueued || state_[i] == State::kRunning ? 1u : 0u;
		}
		return n;
	}

private:
	State state_[kCount] = {};
	std::uint32_t order_[kCount] = {};
	std::uint32_t sequence_ = 0;
};

// The window's counts.
struct Counts;
int FormatSummary(const Counts& c, Mode mode, std::uint32_t leadMs, std::uint64_t ticksPerSecond, char* out, std::size_t capacity);

struct Counts {
	std::uint64_t lines = 0;          // calls the route took
	std::uint64_t left = 0;           // calls left to the game's function (no file, not an MP3 it takes, no room)
	std::uint64_t failed = 0;         // the buffer could not be made or locked: 0 returned, as the original
	std::uint64_t whole = 0;          // lines decoded whole at the call (short, or every slot taken)
	std::uint64_t noSlot = 0;         // ... of them because every slot was taken
	std::uint64_t leadFrames = 0;     // frames decoded at the call
	std::uint64_t workerFrames = 0;   // frames decoded by the worker
	std::uint64_t cancelled = 0;      // lines whose sound was destroyed before the worker was through
	std::uint64_t lockFailures = 0;   // the worker could not lock the buffer: the line's rest stays silent
	std::uint64_t standIn = 0;        // lines without a voice file that got another plugin's stand-in file (among `lines`)
	std::uint64_t callTicks = 0;
	std::uint64_t longestCallTicks = 0;
};

// "Route: voice decode (...) - N lines, the call N.NNN ms a line (longest
// N.NNN), ..." - the window's line.
inline int FormatSummary(const Counts& c, Mode mode, std::uint32_t leadMs, std::uint64_t ticksPerSecond, char* out, std::size_t capacity) {
	const double toMs = ticksPerSecond != 0 ? 1000.0 / static_cast<double>(ticksPerSecond) : 0.0;
	const std::uint64_t calls = c.lines + c.left + c.failed;
	return std::snprintf(out, capacity,
	                     "Route: voice decode (%s, the first %u ms at the call) - %llu lines, the call %.3f ms a line (longest %.3f); frames at the call "
	                     "%llu, on the worker %llu; whole at the call %llu (%llu for want of a slot), cancelled %llu, left to the game %llu, no buffer "
	                     "%llu, worker lock failures %llu; %llu of the lines without a voice file, on a stand-in",
	                     mode == Mode::kEngineDecoder ? "the game's decoder" : mode == Mode::kOwnDecoder ? "own decoder" : "off", leadMs,
	                     static_cast<unsigned long long>(c.lines), calls != 0 ? static_cast<double>(c.callTicks) * toMs / static_cast<double>(calls) : 0.0,
	                     static_cast<double>(c.longestCallTicks) * toMs, static_cast<unsigned long long>(c.leadFrames),
	                     static_cast<unsigned long long>(c.workerFrames), static_cast<unsigned long long>(c.whole),
	                     static_cast<unsigned long long>(c.noSlot), static_cast<unsigned long long>(c.cancelled),
	                     static_cast<unsigned long long>(c.left), static_cast<unsigned long long>(c.failed),
	                     static_cast<unsigned long long>(c.lockFailures), static_cast<unsigned long long>(c.standIn));
}

// Why a call was left to the game's function.
enum class Left : std::uint8_t {
	kTaken,     // not left: the route took the call
	kPath,      // the path does not fit the original's buffer
	kNoFile,    // the game's existence check finds no MP3 under the path
	kNoMemory,  // no block for the file object
	kNotValid,  // the file object says it is no MP3 it can read
	kNoFrames,  // no frame found in it
	kTooLong,   // more frames than a buffer's byte count holds
	kRate,      // no sample rate in its header
	kNoData,    // the file object holds no bytes
};

constexpr const char* LeftName(Left why) {
	switch (why) {
		case Left::kTaken: return "taken";
		case Left::kPath: return "the path does not fit";
		case Left::kNoFile: return "no MP3 under the path";
		case Left::kNoMemory: return "no memory for the file object";
		case Left::kNotValid: return "not an MP3 the game reads";
		case Left::kNoFrames: return "no frames";
		case Left::kTooLong: return "too many frames";
		case Left::kRate: return "no sample rate";
		case Left::kNoData: return "no bytes";
	}
	return "?";
}

// What the opened file object says, in the order the route asks.
constexpr Left LeftOfFile(bool valid, std::uint32_t frames, std::uint32_t rate, bool hasData) {
	return !valid ? Left::kNotValid : frames == 0 ? Left::kNoFrames : frames > kFramesAtMost ? Left::kTooLong : rate < 1000 ? Left::kRate : !hasData ? Left::kNoData : Left::kTaken;
}

// One line per call left to the game (the first ones of a session): the path
// it was called with, why the route did not take it, what the game's function
// returned (the line's length; 0: it played nothing) and how long it ran. A
// line the game plays although the route found no file was given another
// file by someone inside the game's function - a plugin's patch there.
inline int FormatLeft(const char* path, Left why, std::uint32_t resultMs, double callMs, char* out, std::size_t capacity) {
	return std::snprintf(out, capacity, "Route: voice decode - left to the game: \"%s\" (%s); the game's function returned %u ms after %.3f ms%s",
	                     path != nullptr ? path : "", LeftName(why), resultMs, callMs,
	                     why == Left::kNoFile && resultMs != 0 ? " - it played a file the route did not find" : "");
}

// The check's line for one voice file ([Route] VoiceDecodeCheck): the own
// decoder's samples against the game's, frame by frame.
struct Difference {
	std::uint64_t samples = 0;
	std::uint64_t sumSquares = 0;
	std::uint32_t largest = 0;
	std::uint32_t frames = 0;

	void Add(const std::int16_t* a, const std::int16_t* b, std::uint32_t count) {
		for (std::uint32_t i = 0; i < count; ++i) {
			const std::int32_t d = static_cast<std::int32_t>(a[i]) - static_cast<std::int32_t>(b[i]);
			const std::uint32_t size = static_cast<std::uint32_t>(d < 0 ? -d : d);
			largest = size > largest ? size : largest;
			sumSquares += static_cast<std::uint64_t>(size) * size;
		}
		samples += count;
		++frames;
	}

	double MeanSquare() const { return samples != 0 ? static_cast<double>(sumSquares) / static_cast<double>(samples) : 0.0; }
};

}  // namespace obng::engine::sound::voice_decode
