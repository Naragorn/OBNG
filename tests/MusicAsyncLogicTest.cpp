// Every flow of the music's off-thread track start (src/engine/sound/MusicAsyncLogic.h).

#include "Check.h"
#include "engine/sound/MusicAsyncLogic.h"

using namespace obng::engine::sound::music_async;

int main() {
	Section("The INI's value");
	Check(ModeOf(0) == Mode::Off && ModeOf(1) == Mode::On, "0 off, 1 on");
	Check(ModeOf(2) == Mode::Off && ModeOf(-1) == Mode::Off, "anything else: off");

	Section("Nothing asked before the graph is there");
	const Plan none = PlanOf(Intent{});
	Check(!none.volume && !none.position && !none.run && !none.pause && !none.stop, "nothing to give");
	Check(StateOf(Intent{}) == Run::Stopped, "a new graph is stopped");

	Section("The game's own start: the volume, the position 0, Run");
	Intent start;
	AskVolume(start, -1200);
	AskPosition(start, 0.0);
	AskRun(start, Run::Running);
	const Plan started = PlanOf(start);
	Check(started.volume && start.volume == -1200, "the volume given");
	Check(started.position && start.position == 0.0, "the position given");
	Check(started.run && !started.pause && !started.stop, "Run given, nothing else");
	Check(StateOf(start) == Run::Running, "GetState says running");

	Section("The last ask wins");
	Intent changed;
	AskVolume(changed, -1200);
	AskVolume(changed, -300);
	AskRun(changed, Run::Running);
	AskRun(changed, Run::Paused);
	const Plan paused = PlanOf(changed);
	Check(changed.volume == -300, "the fade's last volume");
	Check(paused.pause && !paused.run && !paused.stop, "paused by a menu before the graph was there: Pause");
	Intent stopped;
	AskRun(stopped, Run::Running);
	AskRun(stopped, Run::Stopped);
	const Plan stop = PlanOf(stopped);
	Check(stop.stop && !stop.run && !stop.pause, "stopped before it was there: Stop");
	Check(!stop.volume && !stop.position, "no volume, no position asked: none given");

	Section("GetEvent");
	bool answered = false;
	const EventAnswer building = AnswerEvent(Phase::Building, &answered);
	Check(!building.forward && !building.event && !answered, "building: no event");
	const EventAnswer ready = AnswerEvent(Phase::Ready, &answered);
	Check(ready.forward && !ready.event, "ready: the real graph answers");
	const EventAnswer failed = AnswerEvent(Phase::Failed, &answered);
	Check(!failed.forward && failed.event && failed.code == kErrorAbort && answered, "could not be built: one EC_ERRORABORT");
	const EventAnswer again = AnswerEvent(Phase::Failed, &answered);
	Check(!again.forward && !again.event, "and none after it");
	Check(!AnswerEvent(Phase::Failed, nullptr).event, "no place to remember it: no event");
	return Failures();
}
