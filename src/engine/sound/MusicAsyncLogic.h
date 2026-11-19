#pragma once
// [Route] MusicAsync: the music player's track start without the main thread waiting for DirectShow to build the
// track's graph - the decisions over plain values (the plugin's MusicAsync.cpp is the caller).
//
// Read 2026-11-19: the music player (0x006AB160, 0x006ACD10, 0x006AD030; seven sites, `call [0x00A283C8]` at
// 0x006AB366, 0x006ACF46, 0x006AD216, 0x006AD4F8, 0x006AD806, 0x006ADB76, 0x006ADD56) starts a track with
// CoCreateInstance(CLSID_FilterGraph, IID_IGraphBuilder, this+0x70), IGraphBuilder::RenderFile on the MP3, a
// QueryInterface for IBasicAudio into this+0x74 and put_Volume; 0x006AB420 then asks for IMediaControl and
// IMediaPosition, puts the position to 0 and calls Run; the update 0x006AD030 polls IMediaEvent::GetEvent each frame
// and stops the track on the codes 1 - 3 (0x006A8DB0); menus Pause (0x006A8D00) and Run (0x006A8D50) it. Measured
// (run 20261119-224946, Linux): CoCreateInstance 0.4 ms, RenderFile 27 - 28 ms a track (102 ms the title's).
//
// The route hands the game a stand-in graph at once and builds the real one (CoCreateInstance and RenderFile) on a
// worker thread. What the game asks of the stand-in before the real graph is there - the volume, the position, run,
// pause or stop - is kept and given to the real graph, in the game's own order (volume, position, the run state), on
// the main thread at the first call after it is ready (the update's GetEvent every frame). Until then GetEvent has no
// event; a graph the worker could not build answers one EC_ERRORABORT, so the game stops it and starts another track,
// as it does when its own RenderFile fails (one frame later). Class B: the same track, the same calls in the same order,
// the sound starts a frame or two later.
//
// 64-bit clean: plain values only.
#include <cstdint>

namespace obng::engine::sound::music_async {

enum class Mode : std::uint8_t { Off, On };

inline Mode ModeOf(int value) {
	return value == 1 ? Mode::On : Mode::Off;
}

// The real graph's state as the game last asked for it.
enum class Run : std::uint8_t { Stopped, Paused, Running };

// What the game asked of the stand-in while the real graph was being built.
struct Intent {
	bool hasVolume = false;
	long volume = 0;
	bool hasPosition = false;
	double position = 0.0;
	bool runAsked = false;  // Run, Pause or Stop was called
	Run run = Run::Stopped;
};

inline void AskVolume(Intent& intent, long volume) {
	intent.hasVolume = true;
	intent.volume = volume;
}

inline void AskPosition(Intent& intent, double position) {
	intent.hasPosition = true;
	intent.position = position;
}

inline void AskRun(Intent& intent, Run run) {
	intent.runAsked = true;
	intent.run = run;
}

// The steps for the real graph once it is there, in the game's order: the volume, the position, then the run state
// (Stop only when one was asked: a new graph is stopped already).
struct Plan {
	bool volume = false;
	bool position = false;
	bool run = false;
	bool pause = false;
	bool stop = false;
};

inline Plan PlanOf(const Intent& intent) {
	Plan plan;
	plan.volume = intent.hasVolume;
	plan.position = intent.hasPosition;
	plan.run = intent.runAsked && intent.run == Run::Running;
	plan.pause = intent.runAsked && intent.run == Run::Paused;
	plan.stop = intent.runAsked && intent.run == Run::Stopped;
	return plan;
}

// The stand-in's own state for GetState before the real graph is there: what the game last asked.
inline Run StateOf(const Intent& intent) {
	return intent.runAsked ? intent.run : Run::Stopped;
}

enum class Phase : std::uint8_t { Building, Ready, Failed };

// GetEvent before the real graph answers, or for a graph that could not be built.
struct EventAnswer {
	bool forward = false;  // the real graph answers
	bool event = false;    // the stand-in answers one event
	long code = 0;
};

constexpr long kErrorAbort = 3;  // EC_ERRORABORT (evcode.h)

inline EventAnswer AnswerEvent(Phase phase, bool* errorAnswered) {
	EventAnswer answer;
	if (phase == Phase::Ready) {
		answer.forward = true;
		return answer;
	}
	if (phase == Phase::Failed && errorAnswered != nullptr && !*errorAnswered) {
		*errorAnswered = true;
		answer.event = true;
		answer.code = kErrorAbort;
	}
	return answer;
}

}  // namespace obng::engine::sound::music_async
