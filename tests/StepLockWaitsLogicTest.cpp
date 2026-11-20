// Every flow of the main thread's lock waits a step (src/engine/memory/StepLockWaitsLogic.h).

#include "Check.h"
#include "engine/memory/StepLockWaitsLogic.h"

using namespace obng::engine::memory::step_lock_waits;

int main() {
	Section("An empty step");
	StepWaits step;
	Check(TotalUs(step) == 0 && !Worth(step, 1), "no waits: nothing to write");
	Check(Worth(step, 0), "a floor of 0 writes every step");

	Section("One section");
	Add(step, 0x00B32B80u, 300, 0x00401AF6u);
	Add(step, 0x00B32B80u, 900, 0x00401D92u);
	Add(step, 0x00B32B80u, 500, 0x00401AF6u);
	Check(step.used == 1 && step.slots[0].waits == 3 && step.slots[0].us == 1700, "its waits and time add up");
	Check(step.slots[0].longestUs == 900 && step.slots[0].longestSite == 0x00401D92u, "the longest wait keeps its site");

	Section("Several sections");
	Add(step, 0x18B05F0Cu, 1200, 0x00763FF5u);
	Check(step.used == 2 && step.slots[1].section == 0x18B05F0Cu && step.slots[1].waits == 1, "a second section takes its own slot");
	Check(TotalUs(step) == 2900, "the total over the sections");
	Check(Worth(step, 2900) && !Worth(step, 2901), "written at the floor, not under it");

	Section("More sections than slots");
	for (std::uint32_t i = 0; i < kSlots; ++i) {
		Add(step, 0x1000u + i, 10, 0x2000u + i);
	}
	Check(step.used == kSlots, "the slots are full");
	Check(step.otherWaits == 2 && step.otherUs == 20, "the sections past them counted together");
	Check(TotalUs(step) == 2900 + kSlots * 10, "and in the total");
	Add(step, 0x00B32B80u, 5, 0x00401AF6u);
	Check(step.slots[0].waits == 4, "a known section still finds its slot");

	Section("Reset");
	Reset(step);
	Check(step.used == 0 && step.otherWaits == 0 && TotalUs(step) == 0, "the next step starts empty");
	return Failures();
}
