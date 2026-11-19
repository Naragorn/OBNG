// Every flow of the animation setup's pause batch (src/engine/io/AnimationPauseBatchLogic.h).

#include "Check.h"
#include "engine/io/AnimationPauseBatchLogic.h"

using namespace obng::engine::io::animation_pause_batch;

int main() {
	Section("Outside a setup");
	Batch outside;
	Check(!HoldResume(outside) && outside.held == 0, "a resume goes through");
	Check(Leave(outside) == 0 && outside.depth == 0, "a leave without an enter gives nothing and stays at 0");

	Section("One setup");
	Batch one;
	Enter(one);
	Check(Leave(one) == 0, "no miss: nothing held, nothing to give");
	Enter(one);
	Check(HoldResume(one) && HoldResume(one) && HoldResume(one), "three misses: their resumes held");
	Check(Leave(one) == 3 && one.held == 0 && one.depth == 0, "the end gives the three");

	Section("A setup inside a setup");
	Batch nested;
	Enter(nested);
	Check(HoldResume(nested), "the outer's miss held");
	Enter(nested);
	Check(HoldResume(nested), "the inner's miss held");
	Check(Leave(nested) == 0 && nested.held == 2, "the inner's end gives nothing yet");
	Check(Leave(nested) == 2 && nested.held == 0, "the outer's end gives both");
	return Failures();
}
