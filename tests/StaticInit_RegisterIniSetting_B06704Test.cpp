// Tests of the rebuild of 0x009DC2D0 (proposed StaticInit_RegisterIniSetting_B06704),
// the static initialiser that hands the INI setting object at 0x00B06704 to the
// setting collection at 0x00B07BF0 and registers 0x00A18690 with _atexit.
//
// The body has no branches and no catch (read, 0x009DC2D0..0x009DC323). Its flows are:
//   1. the straight flow: add the setting, register the exit stub, unlink the
//      EH frame (0x009DC318), return;
//   2. the exceptional exit. The frame (handler 0x009AF43A, read: push at
//      0x009DC2D2) is at try-state 0 from 0x009DC2FA until the unlink (read).
//      Whether it is unwound depends on the callers, not on this body (derived
//      from the two-pass model, see the logic header):
//        2a/2b. a handler further out accepts the exception: the frame is unwound
//               from state 0 before that handler's body runs - raised in
//               0x004040E0, or (derived, the state is never reset) in _atexit;
//        2c. no handler further out accepts it: terminate runs and the frame is
//            never unwound - with no handler at all, or with one for another type.
//      What state 0's unwind action does, and whether it is a null action, is
//      unverified (the unwind map behind 0x009AF43A is not in the export). The
//      tests check that the rebuild reaches the port that stands for it, and
//      when - not what it does.
//   3. the ignored result: _atexit's return value is never read (no EAX use
//      after 0x009DC30C, read), so a failed registration still returns normally.
//
// Not covered: a structured (SEH) exception, e.g. an access violation inside
// 0x004040E0. The test build (Linux, clang -m32) has none to raise; see the
// logic header.

#include "Check.h"
#include "engine/time/StaticInit_RegisterIniSetting_B06704.h"
#include "engine/time/StaticInit_RegisterIniSetting_B06704Logic.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

namespace logic = obng::engine::time::static_init_b06704;
namespace engine = obng::engine::time::static_init_b06704::engine;

namespace {

struct ThrownByPort {};

enum class Event { AddSetting, RegisterAtExit, UnwindStateZero };

// Records every call the rebuild makes; each call port can be told to throw.
struct FakePorts {
	std::vector<Event> events;
	std::uint32_t collection = 0;
	std::uint32_t setting = 0;
	std::uint32_t exitFunction = 0;
	int unwindCount = 0;
	bool addSettingThrows = false;
	bool atExitThrows = false;
	int atExitResult = 0;

	void AddSetting(std::uint32_t collectionAddress, std::uint32_t settingAddress) {
		events.push_back(Event::AddSetting);
		collection = collectionAddress;
		setting = settingAddress;
		if (addSettingThrows) {
			throw ThrownByPort{};
		}
	}

	int RegisterAtExit(std::uint32_t exitFunctionAddress) {
		events.push_back(Event::RegisterAtExit);
		exitFunction = exitFunctionAddress;
		if (atExitThrows) {
			throw ThrownByPort{};
		}
		return atExitResult;
	}

	void UnwindStateZero() {
		events.push_back(Event::UnwindStateZero);
		++unwindCount;
	}
};

// A caller that accepts the exception, as a handler further out would.
struct CaughtOutcome {
	bool threw = false;
	int unwindsWhenHandlerRan = -1;  // the unwinds already done when the handler's body runs
};

CaughtOutcome RunUnderCatchingCaller(FakePorts& ports) {
	CaughtOutcome outcome;
	try {
		logic::RunStaticInit(ports, engine::kTargets);
	} catch (const ThrownByPort&) {
		outcome.threw = true;
		outcome.unwindsWhenHandlerRan = ports.unwindCount;
	}
	return outcome;
}

// Flow 2c runs in a child process: with no accepting handler the run ends in
// std::terminate, and the terminate handler reports whether the frame was
// unwound before terminate was called.
constexpr int kTerminatedWithoutUnwind = 40;
constexpr int kTerminatedAfterUnwind = 41;
constexpr int kReturnedNormally = 42;
constexpr int kNoChild = -1;
constexpr int kChildDidNotExit = -2;

FakePorts* terminatingPorts = nullptr;

[[noreturn]] void ReportUnwindAtTerminate() {
	_exit(terminatingPorts->unwindCount == 0 ? kTerminatedWithoutUnwind : kTerminatedAfterUnwind);
}

enum class OuterFrame {
	None,           // nothing further out has a handler
	OtherTypeOnly,  // a handler further out exists, but not for this exception's type
};

void RunUnderNonCatchingCaller(FakePorts& ports, OuterFrame outer) {
	if (outer == OuterFrame::OtherTypeOnly) {
		try {
			logic::RunStaticInit(ports, engine::kTargets);
		} catch (const int&) {
		}
		return;
	}
	logic::RunStaticInit(ports, engine::kTargets);
}

int ExitCodeOfUncaughtRun(FakePorts ports, OuterFrame outer) {
	std::fflush(nullptr);
	pid_t child = fork();
	if (child < 0) {
		return kNoChild;
	}
	if (child == 0) {
		terminatingPorts = &ports;
		std::set_terminate(ReportUnwindAtTerminate);
		RunUnderNonCatchingCaller(ports, outer);
		_exit(kReturnedNormally);
	}
	int status = 0;
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
		return kChildDidNotExit;
	}
	return WEXITSTATUS(status);
}

void StraightFlow() {
	Section("Flow 1: straight - add the setting, register the exit stub, unlink, return");
	FakePorts ports;
	CaughtOutcome outcome = RunUnderCatchingCaller(ports);
	Check(!outcome.threw, "returns normally");
	Check(ports.events == std::vector<Event>{Event::AddSetting, Event::RegisterAtExit},
		"0x004040E0 (call 0x009DC302) runs before _atexit (call 0x009DC30C), nothing else");
	Check(ports.collection == 0x00B07BF0u, "this of 0x004040E0 is g_IniSettingCollection 0x00B07BF0 (ECX at 0x009DC2F5)");
	Check(ports.setting == 0x00B06704u, "the argument is the setting object 0x00B06704 (push at 0x009DC2F0)");
	Check(ports.exitFunction == 0x00A18690u, "_atexit gets the exit stub 0x00A18690 (push at 0x009DC307)");
	Check(ports.unwindCount == 0, "the frame is unlinked at 0x009DC318, not unwound");
}

void IgnoredAtExitResult() {
	Section("Flow 3: a failed _atexit is ignored - no check of EAX after 0x009DC30C");
	FakePorts ports;
	ports.atExitResult = -1;
	CaughtOutcome outcome = RunUnderCatchingCaller(ports);
	Check(!outcome.threw, "returns normally although _atexit reported failure");
	Check(ports.events == std::vector<Event>{Event::AddSetting, Event::RegisterAtExit}, "the same two calls, no fallback");
	Check(ports.unwindCount == 0, "a failure result is not an exception: no unwind");
}

void CaughtFurtherOutFromAddSetting() {
	Section("Flow 2a: 0x004040E0 throws, a handler further out accepts it - the frame is unwound from state 0");
	FakePorts ports;
	ports.addSettingThrows = true;
	CaughtOutcome outcome = RunUnderCatchingCaller(ports);
	Check(outcome.threw, "the exception leaves the initialiser (no catch in this body)");
	Check(ports.events == std::vector<Event>{Event::AddSetting, Event::UnwindStateZero},
		"_atexit is never reached; the state-0 unwind port runs once (what it does: unverified)");
	Check(outcome.unwindsWhenHandlerRan == 1, "the unwind is done before the accepting handler's body runs");
}

void CaughtFurtherOutFromAtExit() {
	Section("Flow 2b: _atexit throws, a handler further out accepts it - state 0 is still live (derived)");
	FakePorts ports;
	ports.atExitThrows = true;
	CaughtOutcome outcome = RunUnderCatchingCaller(ports);
	Check(outcome.threw, "the exception leaves the initialiser");
	Check(ports.events == std::vector<Event>{Event::AddSetting, Event::RegisterAtExit, Event::UnwindStateZero},
		"the setting was added, then the frame is unwound from state 0 (never reset before 0x009DC318)");
	Check(outcome.unwindsWhenHandlerRan == 1, "the unwind is done before the accepting handler's body runs");
}

void NotCaughtFurtherOut() {
	Section("Flow 2c: no handler further out accepts the exception - terminate, the frame is never unwound");
	FakePorts fromAddSetting;
	fromAddSetting.addSettingThrows = true;
	Check(ExitCodeOfUncaughtRun(fromAddSetting, OuterFrame::None) == kTerminatedWithoutUnwind,
		"0x004040E0 throws, no handler at all: terminate runs, state 0 is not unwound");
	Check(ExitCodeOfUncaughtRun(fromAddSetting, OuterFrame::OtherTypeOnly) == kTerminatedWithoutUnwind,
		"0x004040E0 throws, only a handler for another type: terminate runs, state 0 is not unwound");

	FakePorts fromAtExit;
	fromAtExit.atExitThrows = true;
	Check(ExitCodeOfUncaughtRun(fromAtExit, OuterFrame::None) == kTerminatedWithoutUnwind,
		"_atexit throws, no handler at all: terminate runs, state 0 is not unwound");
}

} // namespace

int main() {
	StraightFlow();
	IgnoredAtExitResult();
	CaughtFurtherOutFromAddSetting();
	CaughtFurtherOutFromAtExit();
	NotCaughtFurtherOut();
	return Failures();
}
