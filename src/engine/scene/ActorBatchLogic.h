#pragma once
// The actors' skeleton updates of one frame collected and run together - the
// decisions and the order of the steps, over plain values.
//
// The engine updates one actor after the other in the process manager's loop
// over the high actors 0x00677EC0; inside each actor's update 0x00603CA0 the
// call 0x006043DC runs ActorAnimData::ApplyAnimData 0x00471F20 (read, the
// whole function):
//
//   root = [anim+4], second = [anim+8]; either null: nothing
//   keep root's and second's controller (NiObjectNET +0x0C, a reference each)
//   set both to null (0x00478300)
//   NiAVObject::Update 0x00707370 on root, time = the float at [anim+0x94], flag 1
//   set both back, drop the two references
//
// The references taken and dropped cancel, so what the body leaves is: the
// two controller pointers out for the length of the update and back after it
// (derived from the read above).
//
// The batch: inside the loop the body is not run but noted (root, second,
// the time's bits, a reference on each node so that neither goes before the
// run), and after the loop every noted body runs. What changes against the
// original is the order alone: an actor's own update continues behind the
// call with its skeleton as the frame before left it, and every later
// actor's update sees every earlier skeleton as the frame before left it
// (in the original the later ones see the earlier ones already moved). What
// follows the loop sees every skeleton moved, as in the original.
//
// The run is in three steps so that the middle one can go to several threads
// later: Prepare (one thread: what the down pass needs looked up), Down (per
// job, touching the job's subtree only: controllers out, the subtree's
// update without the parent's upward pass, controllers back), Up (one
// thread, in the jobs' order: the parent's upward pass, which walks nodes
// every actor of a cell shares).
//
// Not deferred, the body runs where it is called: outside the loop, on
// another thread, for the player (whose skeleton OBVR and the first person
// read), for a root that came by already in this loop, for a root the frame before did not batch (a new actor's first
// update runs in place, so its own update never continues with a skeleton
// that was never moved), and when the batch is full.
//
// 64-bit clean: handles are std::uint32_t, no pointer size anywhere.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace obng::engine::scene::actor_batch {

using Handle = std::uint32_t;

struct Job {
	Handle root = 0;
	Handle second = 0;
	std::uint32_t timeBits = 0;
};

enum class Decision : std::uint8_t {
	RunNow,  // the body where it is called
	Defer,   // noted, run after the loop
};

struct Context {
	bool batching = false;        // inside the high actors' loop
	bool loopThread = false;      // on the thread that entered it
	bool isPlayer = false;        // the player's anim data
	bool knownLastFrame = false;  // the root was seen in the loop the frame before
	bool seenThisFrame = false;   // the root came by already in this loop (two jobs on one subtree must not run side by side)
	bool full = false;            // no room for another job
};

constexpr Decision Decide(const Context& c, const Job& job) {
	if (job.root == 0 || job.second == 0) {
		return Decision::RunNow;  // the original's own early-out: nothing happens there
	}
	if (!c.batching || !c.loopThread || c.isPlayer || !c.knownLastFrame || c.seenThisFrame || c.full) {
		return Decision::RunNow;
	}
	return Decision::Defer;
}

// Which calls of the loop 0x00677EC0 batch: the frame's own (the process
// manager 0x00678510 calls it with (0, 0), read: FLDZ 0x0067865A, PUSH 0 0x0067866F; the others pass
// 3600 seconds and 1 - waiting and travelling, where an actor's update is a
// different thing and nothing is gained).
constexpr bool LoopBatches(std::uint32_t timeBits, std::uint32_t flag) {
	return timeBits == 0 && (flag & 0xFFu) == 0;
}

// The roots the frame before saw (deferred or run in place inside the loop).
class KnownRoots {
public:
	bool Contains(Handle root) const { return std::binary_search(last_.begin(), last_.end(), root); }
	bool SeenThisFrame(Handle root) const { return std::find(now_.begin(), now_.end(), root) != now_.end(); }
	// A root seen inside the loop this frame.
	void Saw(Handle root) { now_.push_back(root); }
	// The loop is over: this frame's roots become the known ones.
	void EndFrame() {
		std::sort(now_.begin(), now_.end());
		now_.erase(std::unique(now_.begin(), now_.end()), now_.end());
		last_.swap(now_);
		now_.clear();
	}
	std::size_t Size() const { return last_.size(); }

private:
	std::vector<Handle> last_;
	std::vector<Handle> now_;
};

// What the batch does to the engine:
//   Handle Controller(Handle object)            NiObjectNET +0x0C
//   void SetController(Handle object, Handle)   the pointer alone (the references cancel, see above)
//   void Retain(Handle object) / Release(Handle object)   a reference on a node
//   bool Prepare(const Job&)                    one thread; false: this job cannot run split, RunWhole does it
//   void Down(const Job&)                       the subtree's update without the upward pass
//   void Up(const Job&)                         the parent's upward pass
//   void Whole(const Job&)                      NiAVObject::Update as the original calls it (down and up)
struct Saved {
	Handle rootController = 0;
	Handle secondController = 0;
};

template <typename Port>
Saved Detach(Port& port, const Job& job) {
	Saved saved;
	saved.rootController = port.Controller(job.root);
	saved.secondController = port.Controller(job.second);
	port.SetController(job.root, 0);
	port.SetController(job.second, 0);
	return saved;
}

template <typename Port>
void Attach(Port& port, const Job& job, const Saved& saved) {
	port.SetController(job.root, saved.rootController);
	port.SetController(job.second, saved.secondController);
}

// The middle step for one job: its subtree only.
template <typename Port>
void RunDown(Port& port, const Job& job) {
	const Saved saved = Detach(port, job);
	port.Down(job);
	Attach(port, job, saved);
}

// A job that cannot run split: the body as the original has it.
template <typename Port>
void RunWhole(Port& port, const Job& job) {
	const Saved saved = Detach(port, job);
	port.Whole(job);
	Attach(port, job, saved);
}

class Batch {
public:
	explicit Batch(std::size_t capacity) : capacity_(capacity) { jobs_.reserve(capacity); }

	bool Full() const { return jobs_.size() >= capacity_; }
	std::size_t Size() const { return jobs_.size(); }
	const std::vector<Job>& Jobs() const { return jobs_; }

	// Notes the job and holds its two nodes.
	template <typename Port>
	void Add(Port& port, const Job& job) {
		port.Retain(job.root);
		port.Retain(job.second);
		jobs_.push_back(job);
	}

	// Runs everything noted, in the order noted, and lets the nodes go.
	// `down` runs the middle step over the jobs that can be split - it gets
	// the indices and must have run RunDown for each before it returns (one
	// after the other, or spread over threads).
	template <typename Port, typename DownRunner>
	void Flush(Port& port, DownRunner&& down) {
		split_.clear();
		whole_.clear();
		for (std::size_t i = 0; i < jobs_.size(); ++i) {
			(port.Prepare(jobs_[i]) ? split_ : whole_).push_back(i);
		}
		down(split_);
		// The upward passes and the unsplit bodies in the jobs' order.
		std::size_t s = 0;
		std::size_t w = 0;
		for (std::size_t i = 0; i < jobs_.size(); ++i) {
			if (s < split_.size() && split_[s] == i) {
				port.Up(jobs_[i]);
				++s;
			} else if (w < whole_.size() && whole_[w] == i) {
				RunWhole(port, jobs_[i]);
				++w;
			}
		}
		for (const Job& job : jobs_) {
			port.Release(job.second);
			port.Release(job.root);
		}
		jobs_.clear();
	}

private:
	std::size_t capacity_;
	std::vector<Job> jobs_;
	std::vector<std::size_t> split_;
	std::vector<std::size_t> whole_;
};

}  // namespace obng::engine::scene::actor_batch
