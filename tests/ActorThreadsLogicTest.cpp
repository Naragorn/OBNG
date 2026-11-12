// Every flow of the actors' down passes on threads: the collision objects'
// classes, the written-statics diff, the tally (src/engine/scene/ActorThreadsLogic.h).

#include "Check.h"
#include "engine/scene/ActorThreadsLogic.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace obng::engine::scene::actor_threads;

namespace {

// A living actor's bone: the blend object, in the world, blend 0, the
// previous blend 0, keyframed (motion type 1), mode 0.
CollisionFields Living() {
	CollisionFields f;
	f.mode = 0;
	f.flags = 0x01;
	f.inWorld = true;
	f.blend = 0.0f;
	f.previous = 0.0f;
	f.keepFlag = false;
	f.motionType = 1;
	f.blendClass = true;
	return f;
}

}  // namespace

int main() {
	Section("0x0088EDC0: when the update does nothing");
	{
		CollisionFields f = Living();
		f.mode = 3;
		Check(Classify(f) == CollisionClass::kNothing, "mode 3: returns at once");
		f = Living();
		f.flags = 0x41;
		f.mode = 2;
		Check(Classify(f) == CollisionClass::kNothing, "bit 6 in mode 2: returns");
		f.mode = 0;
		Check(Classify(f) == CollisionClass::kBit6, "bit 6 in mode 0: 0x0089EAE0 runs (unread: not safe)");
		f = Living();
		f.flags = 0x00;
		Check(Classify(f) == CollisionClass::kNothing, "bit 0 clear: returns");
		f = Living();
		f.inWorld = false;
		f.blend = 1.0f;
		f.motionType = 6;
		Check(Classify(f) == CollisionClass::kNothing, "not in the world, blend 1, motion type not 1: slot +0x64 not called");
		f.blend = std::nanf("");
		Check(Classify(f) == CollisionClass::kNothing, "a NaN blend is not below 1: not called either");
	}

	Section("Slot +0x64 of another class than the blend object: not safe");
	{
		CollisionFields f = Living();
		f.blendClass = false;
		Check(Classify(f) == CollisionClass::kOtherClass, "another +0x64: unread");
	}

	Section("0x0088F3D0 at blend 0");
	{
		Check(Classify(Living()) == CollisionClass::kHardSet, "blend 0, previous 0: case 1, the hard set 0x0089EA70 (the node from the body)");
		CollisionFields f = Living();
		f.previous = 0.5f;
		Check(Classify(f) == CollisionClass::kBlendWrite, "previous not 0, mode 0: case 0 - the blended transform written to the node (+0x78)");
		f.mode = 2;
		Check(Classify(f) == CollisionClass::kKeyframeOnly, "case 0 in mode 2: the keyframe alone");
		f.mode = 0;
		f.keepFlag = true;
		Check(Classify(f) == CollisionClass::kHardSet, "the flag +0x0D bit 0 set: case 1");
		f = Living();
		f.mode = 1;
		f.previous = 0.5f;
		Check(Classify(f) == CollisionClass::kNothing, "blend 0 in mode 1: to the end, nothing");
		f = Living();
		f.blend = -0.0f;
		Check(Classify(f) == CollisionClass::kHardSet, "-0 compares equal to 0 on the x87");
	}

	Section("0x0088F3D0 at blend 1 and between");
	{
		CollisionFields f = Living();
		f.blend = 1.0f;
		f.motionType = 6;
		Check(Classify(f) == CollisionClass::kNodeFromBody, "blend 1: case 2, the node from the body (+0x68)");
		f.mode = 2;
		Check(Classify(f) == CollisionClass::kNothing, "blend 1 in mode 2: to the end");
		f = Living();
		f.blend = 0.5f;
		Check(Classify(f) == CollisionClass::kBlendWrite, "between, mode 0: case 0 with the node written");
		f.mode = 2;
		Check(Classify(f) == CollisionClass::kHardSet, "between, mode 2: case 1");
		f = Living();
		f.blend = std::nanf("");
		f.inWorld = true;
		Check(Classify(f) == CollisionClass::kBlendWrite, "a NaN blend in the world: the between path (neither 0 nor 1)");
	}

	Section("A motion type change comes first");
	{
		CollisionFields f = Living();
		f.motionType = 4;
		Check(Classify(f) == CollisionClass::kMotionChange, "wanted 1, has 4: the global list and 0x0089ED20 - not safe");
		f.motionType = 6;
		Check(Classify(f) == CollisionClass::kMotionFrom6, "wanted 1, has 6: +0x68 first, the node from the body");
		f = Living();
		f.blend = 1.0f;
		f.motionType = 1;
		Check(Classify(f) == CollisionClass::kMotionChange, "blend 1 wants 6, has 1: a change");
	}

	Section("Which classes leave the node alone");
	{
		Check(Safe(CollisionClass::kNothing) && Safe(CollisionClass::kKeyframeOnly), "nothing, keyframe only");
		Check(!Safe(CollisionClass::kHardSet), "the hard set 0x0089EA70 writes the node from the body (read 2026-11-12; measured: 13-45 % of its calls change it)");
		Check(!Safe(CollisionClass::kBlendWrite) && !Safe(CollisionClass::kNodeFromBody) && !Safe(CollisionClass::kMotionFrom6) &&
		          !Safe(CollisionClass::kMotionChange) && !Safe(CollisionClass::kBit6) && !Safe(CollisionClass::kOtherClass),
		      "the rest not");
		Check(std::strcmp(kClassName[static_cast<int>(CollisionClass::kHardSet)], "hard set") == 0, "the names");
	}

	Section("The written statics: runs of changed dwords");
	{
		std::uint32_t before[16] = {};
		std::uint32_t after[16] = {};
		WrittenRun runs[4];
		Check(WrittenRuns(before, after, 16, runs, 4) == 0, "nothing changed: no run");
		after[2] = 1;
		after[3] = 2;
		after[7] = 9;
		after[15] = 4;
		const std::uint32_t n = WrittenRuns(before, after, 16, runs, 4);
		Check(n == 3 && runs[0].first == 2 && runs[0].count == 2 && runs[1].first == 7 && runs[1].count == 1 && runs[2].first == 15,
		      "three runs: 2..3, 7, 15");
		WrittenRun one[1];
		Check(WrittenRuns(before, after, 16, one, 1) == 1 && one[0].first == 2, "capacity 1: the first run only");
	}

	Section("The tally of written dwords over the probes");
	{
		WriteTally t;
		t.Note(100, 4);
		t.Note(100, 4);
		t.Note(200, 1);
		std::uint32_t at[2];
		std::uint32_t times[2];
		const std::uint32_t n = t.Most(at, times, 2);
		Check(n == 2 && at[0] == 100 && times[0] == 2 && at[1] == 200 && times[1] == 1, "the most often written first");
		Check(t.Distinct() == 2, "two places");
		for (std::uint32_t i = 0; i < WriteTally::kSlots + 5; ++i) {
			t.Note(1000 + i * 8, 1);
		}
		Check(t.Dropped() > 0, "a full tally counts what it could not keep");
	}

	Section("The summary line");
	{
		Counts c;
		c.jobs = 100;
		c.safeJobs = 80;
		c.classes[static_cast<int>(CollisionClass::kHardSet)] = 1500;
		c.classesWrote[static_cast<int>(CollisionClass::kHardSet)] = 0;
		c.classes[static_cast<int>(CollisionClass::kBlendWrite)] = 30;
		c.classesWrote[static_cast<int>(CollisionClass::kBlendWrite)] = 12;
		c.mispredicted = 0;
		char line[1200];
		FormatSummary(c, 2, true, line, sizeof(line));
		std::printf("    %s\n", line);
		Check(std::strstr(line, "100 skeletons") != nullptr && std::strstr(line, "80 safe") != nullptr, "the skeletons");
		Check(std::strstr(line, "hard set 1500 (0 wrote their node)") != nullptr && std::strstr(line, "blended write 30 (12") != nullptr, "the classes");
	}

	return Failures();
}
