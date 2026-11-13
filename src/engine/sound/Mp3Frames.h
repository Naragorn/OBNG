#pragma once
// An MP3 held in memory, decoded one frame at a time to 1152 mono 16-bit
// samples - the own decoder of [Route] VoiceDecode=2, over minimp3
// (third_party/minimp3, CC0). The game's voice files are MPEG-1 Layer III,
// mono, 32-64 kbit/s, 32000-48000 Hz (what its decoder 0x006B3AE0 accepts,
// read 0x006B37A0-0x006B38A9); a frame of them is 1152 samples, and the
// game's buffer has 0x900 bytes for each.
//
// One translation unit of every binary defines MINIMP3_IMPLEMENTATION before
// including this header (the plugin: Minimp3Impl.cpp).
//
// 64-bit clean: plain values and a pointer to the caller's bytes.
#include <cstdint>
#include <cstring>

#define MINIMP3_ONLY_MP3
#include "../../../third_party/minimp3/minimp3.h"

#include "VoiceDecodeLogic.h"

namespace obng::engine::sound::voice_decode {

struct Mp3Stream {
	mp3dec_t decoder;
	const std::uint8_t* data = nullptr;
	std::uint32_t size = 0;
	std::uint32_t position = 0;
};

inline void Open(Mp3Stream& stream, const std::uint8_t* data, std::uint32_t size) {
	mp3dec_init(&stream.decoder);
	stream.data = data;
	stream.size = data != nullptr ? size : 0;
	stream.position = 0;
}

// The next frame's samples into `out` (kFrameSamples of them). False when
// the data holds no further frame. A frame that gives no sound yet (its main
// data begins before the stream's start) is silence, as it has its place in
// the buffer all the same; a frame of fewer samples or of two channels (not
// a voice file's) is mixed to one channel and padded with silence.
inline bool NextFrame(Mp3Stream& stream, std::int16_t* out) {
	if (stream.data == nullptr || stream.position >= stream.size) {
		return false;
	}
	std::int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	mp3dec_frame_info_t info;
	std::memset(&info, 0, sizeof(info));
	const int samples = mp3dec_decode_frame(&stream.decoder, stream.data + stream.position, static_cast<int>(stream.size - stream.position), pcm, &info);
	// No frame found: minimp3 then reports the bytes it looked through and leaves the frame's facts unset.
	if (info.frame_bytes <= 0 || info.hz == 0) {
		stream.position = stream.size;
		return false;
	}
	stream.position += static_cast<std::uint32_t>(info.frame_bytes);
	std::uint32_t have = samples > 0 ? static_cast<std::uint32_t>(samples) : 0;
	if (have > kFrameSamples) {
		have = kFrameSamples;
	}
	if (have != 0 && info.channels == 1) {
		std::memcpy(out, pcm, have * sizeof(std::int16_t));
	} else if (have != 0) {
		MixToMono(pcm, static_cast<std::uint32_t>(info.channels), have, out);
	}
	if (have < kFrameSamples) {
		std::memset(out + have, 0, (kFrameSamples - have) * sizeof(std::int16_t));
	}
	return true;
}

}  // namespace obng::engine::sound::voice_decode
