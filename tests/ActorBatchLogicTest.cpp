// Every flow of the actor batch's decisions (src/engine/scene/ActorBatchLogic.h).

#include "Check.h"
#include "engine/scene/ActorBatchLogic.h"

#include <map>
#include <string>
#include <vector>

using namespace obng::engine::scene::actor_batch;

namespace {

struct FakePort {
	std::map<Handle, Handle> controllers;
	std::map<Handle, int> references;
	std::vector<Handle> unsplittable;
	std::string calls;

	Handle Controller(Handle object) { return controllers[object]; }
	void SetController(Handle object, Handle controller) {
		controllers[object] = controller;
		calls += "set" + std::to_string(object) + "=" + std::to_string(controller) + " ";
	}
	void Retain(Handle object) { ++references[object]; }
	void Release(Handle object) { --references[object]; }
	bool Prepare(const Job& job) {
		calls += "prepare" + std::to_string(job.root) + " ";
		for (Handle h : unsplittable) {
			if (h == job.root) {
				return false;
			}
		}
		return true;
	}
	void Down(const Job& job) {
		calls += "down" + std::to_string(job.root) + "@" + std::to_string(job.timeBits) + "[" + std::to_string(controllers[job.root]) + "," +
		         std::to_string(controllers[job.second]) + "] ";
	}
	void Up(const Job& job) { calls += "up" + std::to_string(job.root) + " "; }
	void Whole(const Job& job) {
		calls += "whole" + std::to_string(job.root) + "[" + std::to_string(controllers[job.root]) + "," + std::to_string(controllers[job.second]) + "] ";
	}
};

Context Deferring() {
	Context c;
	c.batching = true;
	c.loopThread = true;
	c.knownLastFrame = true;
	return c;
}

void TestDecide() {
	Section("Decide: deferred only inside the loop, on its thread, for a known root that is not the player's, with room");
	const Job job{10, 11, 5};
	Check(Decide(Deferring(), job) == Decision::Defer, "everything given: deferred");
	Check(Decide(Deferring(), Job{0, 11, 5}) == Decision::RunNow, "no root: the original's early-out stays the original's");
	Check(Decide(Deferring(), Job{10, 0, 5}) == Decision::RunNow, "no second node: the same");
	Context c = Deferring();
	c.batching = false;
	Check(Decide(c, job) == Decision::RunNow, "outside the loop: in place");
	c = Deferring();
	c.loopThread = false;
	Check(Decide(c, job) == Decision::RunNow, "another thread: in place");
	c = Deferring();
	c.isPlayer = true;
	Check(Decide(c, job) == Decision::RunNow, "the player: in place");
	c = Deferring();
	c.knownLastFrame = false;
	Check(Decide(c, job) == Decision::RunNow, "a root new this frame: in place");
	c = Deferring();
	c.seenThisFrame = true;
	Check(Decide(c, job) == Decision::RunNow, "a root that came by already in this loop: in place");
	c = Deferring();
	c.full = true;
	Check(Decide(c, job) == Decision::RunNow, "the batch full: in place");
}

void TestLoop() {
	Section("LoopBatches: the frame's own call of the loop only");
	Check(LoopBatches(0, 0), "(0, 0): the process manager's call");
	Check(!LoopBatches(0x45610000, 1), "3600 seconds and 1: waiting, not batched");
	Check(!LoopBatches(0, 1) && !LoopBatches(0x45610000, 0), "either argument set: not batched");
	Check(LoopBatches(0, 0xFFFFFF00u), "the flag is a byte: the stack's upper bytes do not count");
}

void TestKnown() {
	Section("KnownRoots: this frame's roots are known in the next and no longer in the one after");
	KnownRoots known;
	Check(!known.Contains(7) && known.Size() == 0, "nothing known at the start");
	known.Saw(7);
	known.Saw(3);
	known.Saw(7);
	Check(!known.Contains(7), "not within the frame that saw it");
	Check(known.SeenThisFrame(7) && known.SeenThisFrame(3) && !known.SeenThisFrame(5), "but seen this frame");
	known.EndFrame();
	Check(known.Contains(7) && known.Contains(3) && !known.Contains(5) && known.Size() == 2, "known in the next frame, once each");
	Check(!known.SeenThisFrame(7), "the new frame has seen nothing yet");
	known.Saw(5);
	known.EndFrame();
	Check(known.Contains(5) && !known.Contains(7) && !known.Contains(3), "a root not seen for a frame is new again");
	known.EndFrame();
	Check(known.Size() == 0, "an empty frame leaves nothing known");
}

void TestBody() {
	Section("The body: both controllers out for the update and back after it");
	FakePort port;
	port.controllers[10] = 100;
	port.controllers[11] = 110;
	RunDown(port, Job{10, 11, 5});
	Check(port.calls == "set10=0 set11=0 down10@5[0,0] set10=100 set11=110 ", "down: out, the subtree, back");
	port.calls.clear();
	RunWhole(port, Job{10, 11, 5});
	Check(port.calls == "set10=0 set11=0 whole10[0,0] set10=100 set11=110 ", "whole: out, the original's update, back");
	port.calls.clear();
	port.controllers[20] = 200;
	RunDown(port, Job{20, 20, 1});
	Check(port.calls == "set20=0 set20=0 down20@1[0,0] set20=200 set20=200 " && port.controllers[20] == 200, "root and second the same node: its controller survives");
	port.calls.clear();
	RunDown(port, Job{30, 31, 1});
	Check(port.controllers[30] == 0 && port.controllers[31] == 0, "no controllers: none after");
}

void TestBatch() {
	Section("The batch: nodes held from Add to Flush, prepare all, down the split ones, then up and the whole ones in order");
	FakePort port;
	port.controllers[10] = 100;
	port.controllers[40] = 400;
	port.unsplittable = {20};
	Batch batch(3);
	Check(!batch.Full() && batch.Size() == 0, "empty at the start");
	batch.Add(port, Job{10, 11, 1});
	batch.Add(port, Job{20, 21, 2});
	Check(!batch.Full(), "room for one more");
	batch.Add(port, Job{40, 41, 3});
	Check(batch.Full() && batch.Size() == 3, "full at its capacity");
	Check(port.references[10] == 1 && port.references[11] == 1 && port.references[20] == 1 && port.references[41] == 1, "every node held once");
	std::vector<std::size_t> seen;
	batch.Flush(port, [&](const std::vector<std::size_t>& split) {
		seen = split;
		port.calls += "| ";
		for (std::size_t i = split.size(); i-- > 0;) {  // any order: the runner's business
			RunDown(port, batch.Jobs()[split[i]]);
		}
		port.calls += "| ";
	});
	Check(seen == std::vector<std::size_t>({0, 2}), "the runner gets the jobs that can be split");
	Check(port.calls ==
	          "prepare10 prepare20 prepare40 | set40=0 set41=0 down40@3[0,0] set40=400 set41=0 set10=0 set11=0 down10@1[0,0] set10=100 set11=0 | "
	          "up10 set20=0 set21=0 whole20[0,0] set20=0 set21=0 up40 ",
	      "the steps in their order");
	Check(port.references[10] == 0 && port.references[11] == 0 && port.references[20] == 0 && port.references[21] == 0 && port.references[40] == 0 &&
	          port.references[41] == 0,
	      "every node let go");
	Check(batch.Size() == 0 && !batch.Full(), "empty after the flush");

	Section("The batch: an empty flush does nothing; a second frame starts clean");
	port.calls.clear();
	bool ran = false;
	batch.Flush(port, [&](const std::vector<std::size_t>& split) { ran = split.empty(); });
	Check(ran && port.calls.empty(), "the runner called with nothing, no port call");
	batch.Add(port, Job{10, 11, 9});
	batch.Flush(port, [&](const std::vector<std::size_t>& split) {
		for (std::size_t i : split) {
			RunDown(port, batch.Jobs()[i]);
		}
	});
	Check(port.calls == "prepare10 set10=0 set11=0 down10@9[0,0] set10=100 set11=0 up10 ", "the next frame's one job alone");

	Section("The batch: only unsplittable jobs");
	FakePort whole;
	whole.unsplittable = {1, 2};
	Batch b2(8);
	b2.Add(whole, Job{1, 5, 0});
	b2.Add(whole, Job{2, 6, 0});
	b2.Flush(whole, [&](const std::vector<std::size_t>& split) { Check(split.empty(), "nothing for the runner"); });
	Check(whole.calls == "prepare1 prepare2 set1=0 set5=0 whole1[0,0] set1=0 set5=0 set2=0 set6=0 whole2[0,0] set2=0 set6=0 ", "each as the original, in order");
}

}  // namespace

int main() {
	TestDecide();
	TestLoop();
	TestKnown();
	TestBody();
	TestBatch();
	return Failures();
}
