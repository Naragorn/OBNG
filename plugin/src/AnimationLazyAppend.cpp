#include "AnimationLazyAppend.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "engine/io/AnimationLazyAppendLogic.h"

namespace obng::plugin {

namespace {

namespace la = obng::engine::io::lazy_append;

constexpr UInt32 kAppendSite = 0x00474348;  // 0x00474070's call of the list append, ECX = the animation data's list head (read)
constexpr UInt32 kAppend = 0x0067B1E0;      // __thiscall(head, item), RET 4 (read)
constexpr UInt32 kGameAlloc = 0x00401F00;   // __cdecl(size) (read: the append's own call at 0x0067B207)

using AllocFn = void*(__cdecl*)(UInt32 size);

// A list node as the engine has it: {item, next}; the head is one of them, inside its owner.
struct Node {
	UInt32 item;
	UInt32 next;
};

LogFn g_log = nullptr;
bool g_on = false;
la::Mode g_mode = la::Mode::Off;
thread_local la::Scope t_scope;
std::atomic<unsigned long long> g_appends{0}, g_steps{0}, g_fromLast{0}, g_compared{0}, g_mismatches{0};

Node* NodeAt(UInt32 handle) {
	return reinterpret_cast<Node*>(static_cast<std::uintptr_t>(handle));
}

// In place of the engine's append: the same list, the walk started at the kept last node inside a set up.
void __fastcall Append(void* head, void*, UInt32 item) {
	if (la::PutOf(item, 1) == la::Put::Nothing) {
		return;
	}
	const UInt32 headHandle = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(head));
	const UInt32 start = la::StartOf(t_scope, headHandle);
	const auto next = [](UInt32 node) { return NodeAt(node)->next; };
	std::uint64_t steps = 0;
	UInt32 last = la::LastOf(start, next, steps);
	if (g_mode == la::Mode::Check) {
		// The game's own walk decides; the kept end's answer is only compared with it.
		std::uint64_t walked = 0;
		const UInt32 trueLast = la::LastOf(headHandle, next, walked);
		if (start != headHandle) {
			g_compared.fetch_add(1, std::memory_order_relaxed);
			if (last != trueLast) {
				g_mismatches.fetch_add(1, std::memory_order_relaxed);
			}
		}
		last = trueLast;
		steps = walked;
	}
	UInt32 kept = last;
	if (la::PutOf(item, NodeAt(last)->item) == la::Put::IntoLast) {
		NodeAt(last)->item = item;
	} else {
		Node* fresh = static_cast<Node*>(reinterpret_cast<AllocFn>(static_cast<std::uintptr_t>(kGameAlloc))(sizeof(Node)));
		if (fresh != nullptr) {
			fresh->item = item;
			fresh->next = 0;
			kept = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(fresh));
		}
		NodeAt(last)->next = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(fresh));
	}
	la::Keep(t_scope, headHandle, kept);
	g_appends.fetch_add(1, std::memory_order_relaxed);
	g_steps.fetch_add(steps, std::memory_order_relaxed);
	if (start != headHandle) {
		g_fromLast.fetch_add(1, std::memory_order_relaxed);
	}
}

}  // namespace

void AnimationSetupBegins() {
	la::Begin(t_scope);
}

void AnimationSetupEnds() {
	la::End(t_scope);
}

void InstallAnimationLazyAppend(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_mode = la::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "AnimationLazyAppend", la::Shipped(), iniPath)));
	if (g_mode == la::Mode::Off) {
		log("Route: animation lazy append - off in OBNG.ini; the list of animation files to clone is walked from its head at every append, as the game does it");
		return;
	}
	g_log = log;
	UInt8 expected[5] = {0xE8}, patch[5] = {0xE8};
	const UInt32 toOriginal = kAppend - (kAppendSite + 5);
	const UInt32 toRoute = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&Append)) - (kAppendSite + 5);
	std::memcpy(expected + 1, &toOriginal, 4);
	std::memcpy(patch + 1, &toRoute, 4);
	if (!Verify(kAppendSite, expected, sizeof(expected))) {
		log("Route: animation lazy append - the append's call at %08X is not the game's own; not routed", kAppendSite);
		g_mode = la::Mode::Off;
		return;
	}
	if (!SafeWrite(kAppendSite, patch, sizeof(patch))) {
		log("Route: animation lazy append - the append's call at %08X could not be written; not routed", kAppendSite);
		g_mode = la::Mode::Off;
		return;
	}
	g_on = true;
	log("Route: animation lazy append (%s) - inside an animation set up the list of files to clone is %s (needs [Route] AnimationPauseBatch for the "
	    "set up's begin and end)",
	    g_mode == la::Mode::Check ? "check" : "route",
	    g_mode == la::Mode::Check ? "walked as the game walks it, and the kept end compared with the walk's" : "appended to at its kept end");
}

void LogAnimationLazyAppendSummary() {
	if (!g_on) {
		return;
	}
	g_log("Route: animation lazy append (%s) - since the start %llu appends, %llu of them from the kept end, %llu nodes passed on the way in all; "
	      "compared with the game's walk %llu, mismatches %llu",
	      g_mode == la::Mode::Check ? "check" : "route", g_appends.load(std::memory_order_relaxed), g_fromLast.load(std::memory_order_relaxed),
	      g_steps.load(std::memory_order_relaxed), g_compared.load(std::memory_order_relaxed), g_mismatches.load(std::memory_order_relaxed));
}

}  // namespace obng::plugin
