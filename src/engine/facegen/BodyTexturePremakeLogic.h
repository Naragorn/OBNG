#pragma once
// [Route] BodyTexturePremake: an NPC's own body textures made on the loader's thread while its queued head task
// runs, so the character's end on the main thread finds them - the decisions over plain values (the plugin's
// BodyTexturePremake.cpp).
//
// Read 2026-11-20: a queued character's end attaches the actor's 3D on the main thread; its skin parts 0x0047AC20
// call the race's 0x0052D2C0 (`__thiscall(race, base out, own out, npc, part)`, RET 0x10) for each of upper body,
// lower body, hand, foot and tail. That gets the race's texture (0x00442890) and looks the NPC's own one up in the
// texture map [0x00B35300] (its find takes the map's lock 0x00B35380) by "%08XModUpperBodyFemale" / "...Male" for the
// upper body and "%08XModBody" for the rest; when it is not there it fetches the body's .egt model (0x00553620),
// synthesises the texture (0x00557E60, 0x0054DA40) and adds it to the map. Inputs are the NPC's form and the race
// only, not the reference or its 3D.
//
// Measured 2026-11-20 (knowledge/background-loading.md): of 90 ms a ride in 0x0052D2C0 on the main thread, 59 ms are
// the model's fetch (waits on FaceGen's cache lock, .egt reads) and 25.5 ms the texture's making.
//
// The task that runs on the loader's thread for an NPC is its queued head (QueuedHead, vtable 0x00A36CE4; an IO
// task's slot 1 is its run there, 0x00435300 "QueuedHead::Run()", the NPC at task +0x20). A queued character itself
// has none: its slot 11 0x0043DC00 queues its children on the main thread (measured 2026-11-20, run
// obng-profile-ride-anvil-late/20261120-182603: it runs on thread 432, the loader's is 584), slot 10 is the pump's
// poll, slot 5 the end. The route calls 0x0052D2C0 for the five parts after the head's run and lets go of what it
// returns; the map keeps the textures. With bUseMultiThreadedFaceGen on the game itself synthesises the head's
// texture in that run (0x00523220 -> 0x0052DED0 -> 0x005551C0 -> 0x00557E60), so a synthesis on that thread is the
// engine's own design. Only on the thread 0x00442890 takes for the loader's ([[0x00B33A10] + 0x30]): from any other a
// texture miss would pause the loader.
//
// 0x0052D2C0 can only be called the way the engine calls it. Blockhead (shadeMe's OBSE plugin, in Nadi's mod list;
// read in its source, BodyOverride.cpp) hooks the race texture's path at 0x0052D4C9 and takes the body part and the
// NPC not from the function's arguments but from the caller's stack, through the base pointer the function keeps in
// EBP: `[ebp - 0x1C]` and `[ebp - 0x20]`. At the engine's one call (0x0047ADF3 - 0x0047ADFF: push part, push npc, push
// &own, push &base) the base local sits 0x18 above the stack top the arguments are pushed from, so those two words
// are the pushed part and NPC. A call from anywhere else hands Blockhead whatever lies there, its assertion fails and
// its message box stands on the calling thread for good. Measured 2026-11-20 with [Probe] HangWatch (run
// obng-profile-ride-anvil-late/20261120-191622): the loader's thread in user32 under Blockhead.dll with "Assertion
// failed" on its stack, holding OBNG's lock; the main thread in the loader's pause. So OBNG calls the function from a
// thunk with the engine's stack shape (kBaseAboveArguments), and its wrapper of the engine's own call leaves the
// engine's pushed arguments where they are.
//
// The function must not run on two threads at once either: the synthesis gives up when another thread has the
// model's flag (+0x18; 0x00557F64, no texture), and Blockhead's hook at 0x0052D612 passes the body's .egt path through
// one static TESModel written at every call. The game never runs it twice at once (all body textures are made on the
// main thread), so the main thread's call and the premake take turns on a lock of OBNG's around the whole function.
// The premake only tries the lock and skips the NPC when the main thread is inside: the race texture's load in
// 0x0052D2C0, missed on the main thread, pauses the loader until its running task ends (0x00432860), and a task that
// waited for the lock would never end (derived from the read; not seen in a run). The main thread waits for a
// premake in progress, one NPC at most; nothing in the premake waits for the main thread.
// Class B: the same textures, made earlier and on another thread.
#include <cstddef>
#include <cstdint>

namespace obng::engine::facegen::premake {

// What OBNG runs with when OBNG.ini has no key.
constexpr int Shipped() {
	return 0;
}

constexpr bool OnOf(int ini) {
	return ini == 1;
}

constexpr std::uint8_t kNpcFormType = 0x23;

// The engine's call shape that Blockhead's hook reads through: with E the stack top before the four arguments are
// pushed, the base local is at E + kBaseAboveArguments, the pushed part at E - 4 and the pushed NPC at E - 8.
constexpr std::uint32_t kBaseAboveArguments = 0x18;
constexpr std::uint32_t kPartBelowBase = 0x1C;  // Blockhead's `[ebp - 0x1C]`
constexpr std::uint32_t kNpcBelowBase = 0x20;   // Blockhead's `[ebp - 0x20]`
static_assert(kBaseAboveArguments + 4 == kPartBelowBase && kBaseAboveArguments + 8 == kNpcBelowBase,
              "the base local's place is the one that makes Blockhead's two reads the pushed part and NPC");

// The parts 0x0047AC20 asks 0x0052D2C0 for: upper body, lower body, hand, foot, tail.
constexpr int kParts[] = {2, 3, 4, 5, 15};
constexpr std::size_t kPartCount = sizeof(kParts) / sizeof(kParts[0]);

enum class Skip { None = 0, NoNpc, NotAnNpc, NoRace, TexturingOff, NotTheLoaderThread, AlreadyMade, MainThreadInside, Count };

// The name 0x0052D2C0 keeps an NPC's own texture under in the texture map (read: 0x0052D5C0 - 0x0052D60D): the upper
// body's by sex, one for every other part. False for a part the function makes no texture for.
constexpr std::size_t kKeySize = 32;
inline bool KeyOf(int part, bool female, std::uint32_t formId, char (&out)[kKeySize]) {
	const bool known = (part >= 2 && part <= 6) || part == 15;
	if (!known) {
		out[0] = '\0';
		return false;
	}
	const char* digits = "0123456789ABCDEF";
	for (int i = 0; i < 8; ++i) {
		out[i] = digits[(formId >> (28 - 4 * i)) & 0xF];
	}
	const char* tail = part != 2 ? "ModBody" : female ? "ModUpperBodyFemale" : "ModUpperBodyMale";
	std::size_t at = 8;
	for (; *tail != '\0'; ++tail) {
		out[at++] = *tail;
	}
	out[at] = '\0';
	return true;
}

struct Facts {
	bool npc = false;             // the task has an NPC
	std::uint8_t formType = 0;    // the base form's type byte
	bool race = false;            // the NPC has a race
	bool texturing = false;       // bFaceGenTexturing
	bool loaderThread = false;    // the current thread is the one 0x00442890 loads on without a pause
	bool alreadyMade = false;     // both of the NPC's textures are in the map
	bool mainThreadInside = false;  // OBNG's lock could not be taken at once: the main thread is in 0x0052D2C0
};

constexpr Skip Decide(const Facts& facts) {
	if (!facts.npc) {
		return Skip::NoNpc;
	}
	if (facts.formType != kNpcFormType) {
		return Skip::NotAnNpc;
	}
	if (!facts.race) {
		return Skip::NoRace;
	}
	if (!facts.texturing) {
		return Skip::TexturingOff;
	}
	if (!facts.loaderThread) {
		return Skip::NotTheLoaderThread;
	}
	if (facts.alreadyMade) {
		return Skip::AlreadyMade;
	}
	if (facts.mainThreadInside) {
		return Skip::MainThreadInside;
	}
	return Skip::None;
}

struct Totals {
	std::uint64_t made = 0;         // tasks whose textures were asked for
	std::uint64_t withTexture = 0;  // own textures the calls came back with
	std::uint64_t ticks = 0;
	std::uint64_t longest = 0;
	std::uint64_t skipped[static_cast<std::size_t>(Skip::Count)] = {};
};

inline void Count(Totals& totals, Skip skip, std::uint64_t ticks, unsigned textures) {
	if (skip != Skip::None) {
		++totals.skipped[static_cast<std::size_t>(skip)];
		return;
	}
	++totals.made;
	totals.withTexture += textures;
	totals.ticks += ticks;
	totals.longest = ticks > totals.longest ? ticks : totals.longest;
}

}  // namespace obng::engine::facegen::premake
