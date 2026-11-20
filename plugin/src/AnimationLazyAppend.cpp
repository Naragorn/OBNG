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
thread_local la::Scope t_scope;
std::atomic<unsigned long long> g_appends{0}, g_steps{0}, g_fromLast{0};

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
	std::uint64_t steps = 0;
	const UInt32 last = la::LastOf(start, [](UInt32 node) { return NodeAt(node)->next; }, steps);
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
	if (!la::OnOf(static_cast<int>(GetPrivateProfileIntA("Route", "AnimationLazyAppend", la::Shipped(), iniPath)))) {
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
		return;
	}
	if (!SafeWrite(kAppendSite, patch, sizeof(patch))) {
		log("Route: animation lazy append - the append's call at %08X could not be written; not routed", kAppendSite);
		return;
	}
	g_on = true;
	log("Route: animation lazy append - inside an animation set up the list of files to clone is appended to at its kept end (needs [Route] "
	    "AnimationPauseBatch for the set up's begin and end)");
}

void LogAnimationLazyAppendSummary() {
	if (!g_on) {
		return;
	}
	g_log("Route: animation lazy append - since the start %llu appends, %llu of them from the kept end, %llu nodes passed on the way in all",
	      g_appends.load(std::memory_order_relaxed), g_fromLast.load(std::memory_order_relaxed), g_steps.load(std::memory_order_relaxed));
}

}  // namespace obng::plugin
