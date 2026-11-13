// The own decoder's frames (src/engine/sound/Mp3Frames.h over minimp3) against a tone in the voice files' format.

#define MINIMP3_IMPLEMENTATION
#include "Check.h"
#include "data/VoiceTestMp3.h"
#include "engine/sound/Mp3Frames.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace obng::engine::sound::voice_decode;

int main() {
	const std::uint32_t size = sizeof(kVoiceTestMp3);

	Section("The test file is what the game's decoder takes");
	Check(kVoiceTestMp3[0] == 0xFF && kVoiceTestMp3[1] == 0xFB, "an MPEG-1 Layer III frame at the first byte");
	Check((kVoiceTestMp3[3] >> 6) == 3, "mono");
	const std::uint32_t counted = CountFrames(kVoiceTestMp3, size);
	Check(counted == 24, "24 FF FB pairs: the frame count the game would size its buffer by");

	Section("Every frame decodes to 1152 samples; the tone is there");
	{
		Mp3Stream stream;
		Open(stream, kVoiceTestMp3, size);
		std::vector<std::int16_t> all;
		std::int16_t frame[kFrameSamples];
		std::uint32_t frames = 0;
		while (NextFrame(stream, frame)) {
			all.insert(all.end(), frame, frame + kFrameSamples);
			++frames;
		}
		std::printf("    %u frames decoded, %u counted\n", frames, counted);
		Check(frames >= 23 && frames <= counted, "as many frames as the file has, none beyond the game's count");
		Check(!NextFrame(stream, frame), "at the end: no further frame");
		// The tone: 440 Hz at 44100 Hz is 100.2 samples a period - count the rising zero crossings in the middle.
		std::uint32_t crossings = 0;
		std::int32_t peak = 0;
		const std::size_t from = 4 * kFrameSamples;
		const std::size_t to = 20 * kFrameSamples;
		for (std::size_t i = from + 1; i < to && i < all.size(); ++i) {
			crossings += all[i - 1] < 0 && all[i] >= 0 ? 1u : 0u;
			peak = std::abs(all[i]) > peak ? std::abs(all[i]) : peak;
		}
		const double expected = static_cast<double>(to - from) * 440.0 / 44100.0;
		std::printf("    %u rising zero crossings in %zu samples (440 Hz gives %.1f), peak %d\n", crossings, to - from, expected, peak);
		Check(std::fabs(static_cast<double>(crossings) - expected) <= 2.0, "440 Hz");
		Check(peak > 3000 && peak <= 32767, "at an audible level");
	}

	Section("Decoded in two parts with the same stream, the samples are the same (the call's frames, then the worker's)");
	{
		Mp3Stream whole;
		Open(whole, kVoiceTestMp3, size);
		Mp3Stream parts;
		Open(parts, kVoiceTestMp3, size);
		std::int16_t a[kFrameSamples];
		std::int16_t b[kFrameSamples];
		bool same = true;
		std::uint32_t frames = 0;
		for (;;) {
			const bool moreA = NextFrame(whole, a);
			Mp3Stream moved = parts;  // the stream handed on by copy, as the job takes it
			const bool moreB = NextFrame(moved, b);
			parts = moved;
			if (moreA != moreB) {
				same = false;
				break;
			}
			if (!moreA) {
				break;
			}
			same = same && std::memcmp(a, b, sizeof(a)) == 0;
			++frames;
		}
		Check(same && frames >= 23, "bit for bit, through a copy of the stream at every frame");
	}

	Section("No data, cut data, noise");
	{
		Mp3Stream stream;
		std::int16_t frame[kFrameSamples];
		Open(stream, nullptr, 100);
		Check(!NextFrame(stream, frame), "no data: no frame");
		Open(stream, kVoiceTestMp3, 0);
		Check(!NextFrame(stream, frame), "no bytes: no frame");
		Open(stream, kVoiceTestMp3, 100);
		std::uint32_t frames = 0;
		while (NextFrame(stream, frame) && frames < 10) {
			++frames;
		}
		Check(frames <= 1, "a file cut inside its first frame: at most that frame, then the end");
		std::uint8_t noise[2000];
		for (std::size_t i = 0; i < sizeof(noise); ++i) {
			noise[i] = static_cast<std::uint8_t>((i * 37 + 11) & 0x7F);  // never an FF: no sync
		}
		Open(stream, noise, sizeof(noise));
		Check(!NextFrame(stream, frame), "bytes without a frame: none, and it ends");
	}

	return Failures();
}
