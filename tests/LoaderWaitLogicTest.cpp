// Every flow of the loader wait probe's decisions (src/engine/io/LoaderWaitLogic.h).

#include "Check.h"
#include "engine/io/LoaderWaitLogic.h"

using namespace obng::engine::io::loader_wait;

int main() {
	Section("Who asked for the pause");
	Check(CallerOf(kPauseAllReturn, 0x00442951u) == 0x00442951u, "through 0x00432860: the word above is its caller");
	Check(CallerOf(0x00444ABCu, 0x12345678u) == 0x00444ABCu, "a direct call: the return address is the caller");

	Section("Which waits are kept");
	Check(Keep(1.5, 0.5, 0, 512), "long enough, room left");
	Check(!Keep(0.2, 0.5, 0, 512), "too short");
	Check(!Keep(9.0, 0.5, 512, 512), "no room left");
	return Failures();
}
