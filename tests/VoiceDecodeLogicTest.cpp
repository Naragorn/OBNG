// Every flow of the voice line's decode off the main thread - the decisions (src/engine/sound/VoiceDecodeLogic.h).

#include "Check.h"
#include "engine/sound/VoiceDecodeLogic.h"

#include <cstring>
#include <string>

using namespace obng::engine::sound::voice_decode;

int main() {
	Section("The mode");
	Check(ModeOf(0, Mode::kOwnDecoder) == Mode::kOff && ModeOf(1, Mode::kOwnDecoder) == Mode::kEngineDecoder && ModeOf(2, Mode::kOff) == Mode::kOwnDecoder,
	      "0 off, 1 the game's decoder, 2 the own one");
	Check(ModeOf(7, Mode::kOwnDecoder) == Mode::kOwnDecoder && ModeOf(-1, Mode::kOff) == Mode::kOff, "anything else: the default");

	Section("The path: copied, the first .wav made .mp3");
	{
		char out[kPathCapacity];
		Check(Mp3Path("data\\sound\\voice\\oblivion.esm\\nord\\m\\generic_hello_00012345_1.wav", out, sizeof(out)) &&
		          std::strcmp(out, "data\\sound\\voice\\oblivion.esm\\nord\\m\\generic_hello_00012345_1.mp3") == 0,
		      "a .wav path becomes the .mp3's");
		Check(Mp3Path("a.mp3", out, sizeof(out)) && std::strcmp(out, "a.mp3") == 0, "an .mp3 path stays");
		Check(Mp3Path("x.wav.wav", out, sizeof(out)) && std::strcmp(out, "x.mp3.wav") == 0, "the first .wav only, as strstr finds it");
		Check(Mp3Path("x.WAV", out, sizeof(out)) && std::strcmp(out, "x.WAV") == 0, "case-sensitive, as the original");
		Check(Mp3Path("", out, sizeof(out)) && out[0] == 0, "an empty path: empty");
		const std::string fits(kPathCapacity - 1, 'a');
		const std::string tooLong(kPathCapacity, 'a');
		Check(Mp3Path(fits.c_str(), out, sizeof(out)) && std::strlen(out) == kPathCapacity - 1, "259 characters fit with the terminator");
		Check(!Mp3Path(tooLong.c_str(), out, sizeof(out)), "260 do not: left to the original");
		Check(!Mp3Path(nullptr, out, sizeof(out)) && !Mp3Path("a", nullptr, 4) && !Mp3Path("a", out, 0), "no path, no room: refused");
		char small[4];
		Check(Mp3Path("abc", small, sizeof(small)) && !Mp3Path("abcd", small, sizeof(small)), "the capacity counts the terminator");
	}

	Section("The buffer's size, flags and the duration, as 0x006A9E00 computes them");
	Check(BufferBytes(100) == 100 * 0x900, "frames * 0x900");
	Check(DurationMs(100, 44100) == (100 * 0x900 / 2) / 44, "44100 Hz: samples / 44 (the original's integer rate / 1000)");
	Check(DurationMs(115, 44100) == 3010, "115 frames at 44100: 3010 ms");
	Check(DurationMs(100, 48000) == 2400 && DurationMs(100, 32000) == 3600, "48000 and 32000");
	Check(DurationMs(100, 999) == 0, "a rate under 1000: 0, where the original would divide by zero");
	Check(DurationMs(3000, 44100) == static_cast<std::uint16_t>((3000u * 0x900u / 2) / 44), "over 65.5 s the low 16 bits, as the original's AX");
	Check(BufferFlags(0) == 0xA0 && BufferFlags(5) == 0xA0, "no 3D: volume and frequency control");
	Check(BufferFlags(2) == 0x200B0 && BufferFlags(6) == 0x200B0 && BufferFlags(0x106) == 0x200B0, "bit 1: 3D and mute at the maximum distance too");
	Check(kFramesAtMost == 0xFFFFFFFFu / 0x900 && BufferBytes(kFramesAtMost) / 0x900 == kFramesAtMost, "the most frames whose byte count does not wrap");

	Section("How many frames are decoded at the call");
	Check(LeadFrames(150, 44100, 115) == 6, "150 ms at 44100 Hz: 6 frames (26.1 ms each)");
	Check(LeadFrames(150, 32000, 115) == 5, "at 32000 Hz: 5 (36 ms each)");
	Check(LeadFrames(0, 44100, 115) == 2 && LeadFrames(10, 44100, 115) == 2, "two at the least");
	Check(LeadFrames(150, 44100, 4) == 4 && LeadFrames(150, 44100, 1) == 1 && LeadFrames(150, 44100, 0) == 0, "never more than the line has");
	Check(LeadFrames(100000, 48000, 100) == 100, "a lead longer than the line: the whole line");

	Section("The worker's pieces");
	Check(NextChunk(6, 115, 8) == 8 && NextChunk(110, 115, 8) == 5 && NextChunk(115, 115, 8) == 0 && NextChunk(120, 115, 8) == 0, "eight frames, the rest, nothing");

	Section("The original's frame count: FF FB pairs");
	{
		const std::uint8_t data[] = {0xFF, 0xFB, 0x90, 0x00, 0xFF, 0xFB, 0xFF, 0xFA, 0xFF, 0xFF, 0xFB, 0xFF};
		Check(CountFrames(data, sizeof(data)) == 3, "three pairs; FF FA is not counted; FF FF FB counts once");
		Check(CountFrames(data, 1) == 0 && CountFrames(data, 0) == 0 && CountFrames(data, 2) == 1, "no pair in less than two bytes");
		const std::uint8_t tail[] = {0x00, 0xFF};
		Check(CountFrames(tail, sizeof(tail)) == 0, "an FF as the last byte is no pair");
	}

	Section("Two channels to one");
	{
		const std::int16_t stereo[] = {100, 300, -100, -300, 32767, 32767, -32768, -32768};
		std::int16_t mono[4] = {};
		MixToMono(stereo, 2, 4, mono);
		Check(mono[0] == 200 && mono[1] == -200 && mono[2] == 32767 && mono[3] == -32768, "the mean, the extremes kept");
		const std::int16_t one[] = {5, -6};
		MixToMono(one, 1, 2, mono);
		Check(mono[0] == 5 && mono[1] == -6, "one channel: itself");
		MixToMono(one, 0, 2, mono);
		Check(mono[0] == 0 && mono[1] == 0, "no channel: silence");
	}

	Section("The lines in work");
	{
		Slots s;
		Check(s.InWork() == 0 && s.Take() == Slots::kNone && s.TakeDone() == Slots::kNone, "nothing at first");
		const std::uint32_t a = s.Claim();
		const std::uint32_t b = s.Claim();
		Check(a != Slots::kNone && b != Slots::kNone && a != b, "two slots claimed");
		Check(s.Of(a) == Slots::State::kClaimed && s.InWork() == 0 && s.Take() == Slots::kNone, "a claimed slot is not the worker's yet");
		s.Queue(b);
		s.Queue(a);
		Check(s.Of(a) == Slots::State::kQueued && s.InWork() == 2, "queued: in work");
		Check(s.Take() == b && s.Of(b) == Slots::State::kRunning, "the worker takes the one queued first, whatever its slot");
		Check(s.InWork() == 2, "a running line is still in work");
		s.Finish(b);
		Check(s.Of(b) == Slots::State::kDone && s.InWork() == 1, "finished: done, no longer in work");
		Check(s.Take() == a, "then the next");
		Check(s.TakeDone() == b && s.Of(b) == Slots::State::kFree && s.TakeDone() == Slots::kNone, "the main thread takes the done one: free again");
		s.Finish(b);
		s.Queue(b);
		s.Drop(b);
		Check(s.Of(b) == Slots::State::kFree, "finishing, queueing or dropping a slot in another state changes nothing");
		s.Finish(Slots::kCount + 3);
		s.Queue(Slots::kCount + 3);
		s.Drop(Slots::kCount + 3);
		s.Finish(a);
		Check(s.TakeDone() == a, "the second done");
		const std::uint32_t c = s.Claim();
		s.Drop(c);
		Check(s.Of(c) == Slots::State::kDone && s.InWork() == 0 && s.TakeDone() == c, "a claimed slot dropped (the line needed no worker): done, to be cleaned");
		std::uint32_t claimed = 0;
		while (s.Claim() != Slots::kNone) {
			++claimed;
		}
		Check(claimed == Slots::kCount, "all slots claimed, then none left");
		Check(s.Of(Slots::kCount) == Slots::State::kFree, "a slot out of range reads as free");
	}

	Section("The summary and the check's difference");
	{
		Counts c;
		c.lines = 10;
		c.left = 1;
		c.failed = 1;
		c.whole = 2;
		c.noSlot = 1;
		c.leadFrames = 60;
		c.workerFrames = 900;
		c.cancelled = 3;
		c.lockFailures = 0;
		c.callTicks = 60000;
		c.longestCallTicks = 9000;
		char line[600];
		FormatSummary(c, Mode::kOwnDecoder, 150, 10000000, line, sizeof(line));
		std::printf("    %s\n", line);
		const std::string text(line);
		Check(text.find("Route: voice decode (own decoder, the first 150 ms at the call) - 10 lines, the call 0.500 ms a line (longest 0.900)") == 0,
		      "the mode, the lead, the lines, the call's mean over all twelve calls and its longest");
		Check(text.find("frames at the call 60, on the worker 900") != std::string::npos && text.find("whole at the call 2 (1 for want of a slot)") != std::string::npos &&
		          text.find("cancelled 3, left to the game 1, no buffer 1, worker lock failures 0") != std::string::npos,
		      "the counts");
		Section("Why a call is left to the game, and its line");
		Check(LeftOfFile(false, 10, 44100, true) == Left::kNotValid && LeftOfFile(true, 0, 44100, true) == Left::kNoFrames &&
		          LeftOfFile(true, kFramesAtMost + 1, 44100, true) == Left::kTooLong && LeftOfFile(true, 10, 999, true) == Left::kRate &&
		          LeftOfFile(true, 10, 44100, false) == Left::kNoData,
		      "each refusal, in the order asked");
		Check(LeftOfFile(true, kFramesAtMost, 1000, true) == Left::kTaken && LeftOfFile(false, 0, 0, false) == Left::kNotValid, "the bounds are taken; the first refusal wins");
		for (const Left why : {Left::kTaken, Left::kPath, Left::kNoFile, Left::kNoMemory, Left::kNotValid, Left::kNoFrames, Left::kTooLong, Left::kRate, Left::kNoData}) {
			Check(std::string(LeftName(why)) != "?" && !std::string(LeftName(why)).empty(), "a name for every reason");
		}
		Check(std::string(LeftName(static_cast<Left>(200))) == "?", "none for a value that is no reason");
		char left[400];
		FormatLeft("Data\\Sound\\Voice\\x.esp\\imperial\\m\\a.wav", Left::kNoFile, 8170, 10.25, left, sizeof(left));
		Check(std::string(left) == "Route: voice decode - left to the game: \"Data\\Sound\\Voice\\x.esp\\imperial\\m\\a.wav\" (no MP3 under the path); the game's function "
		                           "returned 8170 ms after 10.250 ms - it played a file the route did not find",
		      "no file, yet the game played one: said");
		FormatLeft("a.wav", Left::kNoFile, 0, 0.02, left, sizeof(left));
		Check(std::string(left) == "Route: voice decode - left to the game: \"a.wav\" (no MP3 under the path); the game's function returned 0 ms after 0.020 ms",
		      "no file and nothing played: plain");
		FormatLeft(nullptr, Left::kNotValid, 1200, 3.0, left, sizeof(left));
		Check(std::string(left) == "Route: voice decode - left to the game: \"\" (not an MP3 the game reads); the game's function returned 1200 ms after 3.000 ms",
		      "another reason carries no such remark; a null path is the empty one");
		char tiny[16];
		Check(FormatLeft("a.wav", Left::kNoFile, 1, 1.0, tiny, sizeof(tiny)) > static_cast<int>(sizeof(tiny)) && tiny[sizeof(tiny) - 1] == 0, "a buffer too small is not overrun");
		FormatSummary(Counts{}, Mode::kEngineDecoder, 100, 0, line, sizeof(line));
		Check(std::string(line).find("(the game's decoder, the first 100 ms at the call) - 0 lines, the call 0.000 ms") != std::string::npos, "nothing counted, no clock: zeros");
		Difference d;
		const std::int16_t x[] = {0, 100, -100, 32767};
		const std::int16_t y[] = {0, 98, -97, -32768};
		d.Add(x, y, 4);
		Check(d.frames == 1 && d.samples == 4 && d.largest == 65535 && d.sumSquares == 4ull + 9ull + 65535ull * 65535ull, "the largest difference and the sum of squares");
		Check(Difference{}.MeanSquare() == 0.0 && d.MeanSquare() > 1.0e9, "the mean square; none without samples");
	}

	return Failures();
}
