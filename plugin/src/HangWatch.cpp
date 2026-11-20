#include "HangWatch.h"

#include <windows.h>

#include <tlhelp32.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "StepHooks.h"
#include "engine/diagnostics/HangWatchLogic.h"

namespace obng::plugin {

namespace {

namespace hw = obng::engine::diagnostics::hang_watch;

constexpr unsigned kLocks = 48, kThreads = 96, kWords = 1536, kCodeWords = 28;

struct Lock {
	const char* name;
	const CRITICAL_SECTION* section;
};

struct Captured {
	DWORD id;
	UInt32 eip, esp;
	unsigned count;
	UInt32 words[kWords];
};

LogFn g_log = nullptr;
Lock g_locks[kLocks];
std::atomic<unsigned> g_lockCount{0};
std::atomic<unsigned long long> g_steps{0};
Captured g_capture;  // the watch thread's only

void Step() {
	g_steps.fetch_add(1, std::memory_order_relaxed);
}

// The thread held still for the read of its registers and stack; nothing here allocates or takes a lock of the
// process, since the held thread may own any of them.
bool Capture(DWORD id, Captured& out) {
	const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
	if (thread == nullptr) {
		return false;
	}
	bool ok = false;
	if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
		CONTEXT context;
		std::memset(&context, 0, sizeof(context));
		context.ContextFlags = CONTEXT_CONTROL;
		if (GetThreadContext(thread, &context)) {
			out.id = id;
			out.eip = context.Eip;
			out.esp = context.Esp;
			out.count = 0;
			MEMORY_BASIC_INFORMATION info;
			UInt32 at = context.Esp & ~3u;
			while (out.count < kWords && VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), &info, sizeof(info)) != 0
			       && info.State == MEM_COMMIT && (info.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READWRITE)) != 0
			       && (info.Protect & PAGE_GUARD) == 0) {
				const UInt32 end = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(info.BaseAddress)) + static_cast<UInt32>(info.RegionSize);
				for (; at < end && out.count < kWords; at += 4) {
					out.words[out.count++] = *reinterpret_cast<const UInt32*>(static_cast<std::uintptr_t>(at));
				}
			}
			ok = true;
		}
		ResumeThread(thread);
	}
	CloseHandle(thread);
	return ok;
}

// A code address as "module+offset" (the game's own as the address); false for a word that is no code.
bool Describe(UInt32 word, char* out, std::size_t size) {
	if (hw::InExeCode(word)) {
		std::snprintf(out, size, "%08X", word);
		return true;
	}
	if (word < 0x10000) {
		return false;
	}
	MEMORY_BASIC_INFORMATION info;
	if (VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(word)), &info, sizeof(info)) == 0 || info.Type != MEM_IMAGE
	    || (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
		return false;
	}
	char path[MAX_PATH];
	const HMODULE module = static_cast<HMODULE>(info.AllocationBase);
	if (module == GetModuleHandleA(nullptr) || GetModuleFileNameA(module, path, sizeof(path)) == 0) {
		return false;
	}
	const char* name = std::strrchr(path, '\\');
	std::snprintf(out, size, "%s+%X", name != nullptr ? name + 1 : path, word - static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(module)));
	return true;
}

void LogThread(const Captured& thread) {
	char line[1400], one[80];
	int at = 0;
	if (!Describe(thread.eip, one, sizeof(one))) {
		std::snprintf(one, sizeof(one), "%08X", thread.eip);
	}
	at += std::snprintf(line, sizeof(line), "Probe: hang watch - thread %lu at %s, code addresses on its stack (stale ones among them):",
	                    static_cast<unsigned long>(thread.id), one);
	unsigned shown = 0;
	UInt32 last = 0;
	for (unsigned i = 0; i < thread.count && shown < kCodeWords && at > 0 && static_cast<std::size_t>(at) < sizeof(line) - 90; ++i) {
		if (thread.words[i] == last || !Describe(thread.words[i], one, sizeof(one))) {
			continue;
		}
		last = thread.words[i];
		at += std::snprintf(line + at, sizeof(line) - at, " %s", one);
		++shown;
	}
	g_log("%s", line);
}

void Dump(unsigned long long steps) {
	g_log("Probe: hang watch - no update step for %u seconds (the count stands at %llu); the locks' holders, then every thread", hw::kStallMs / 1000, steps);
	const unsigned locks = g_lockCount.load(std::memory_order_acquire);
	for (unsigned i = 0; i < locks; ++i) {
		const CRITICAL_SECTION* section = g_locks[i].section;
		if (section->OwningThread != nullptr || section->LockCount != -1) {
			g_log("Probe: hang watch - lock %s (%08X): held by thread %lu, entered %ld times, lock count %ld", g_locks[i].name,
			      static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(section)),
			      static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(section->OwningThread)), section->RecursionCount, section->LockCount);
		}
	}
	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		g_log("Probe: hang watch - no thread list");
		return;
	}
	DWORD ids[kThreads];
	unsigned count = 0;
	THREADENTRY32 entry;
	entry.dwSize = sizeof(entry);
	for (BOOL more = Thread32First(snapshot, &entry); more && count < kThreads; more = Thread32Next(snapshot, &entry)) {
		if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != GetCurrentThreadId()) {
			ids[count++] = entry.th32ThreadID;
		}
	}
	CloseHandle(snapshot);
	for (unsigned i = 0; i < count; ++i) {
		if (Capture(ids[i], g_capture)) {
			LogThread(g_capture);
		}
	}
	g_log("Probe: hang watch - %u threads written", count);
}

DWORD WINAPI Watch(void*) {
	hw::State state;
	for (;;) {
		Sleep(1000);
		const unsigned long long steps = g_steps.load(std::memory_order_relaxed);
		if (hw::Decide(state, steps, GetTickCount64())) {
			Dump(steps);
		}
	}
}

}  // namespace

void HangWatchLock(const char* name, const void* criticalSection) {
	const unsigned at = g_lockCount.load(std::memory_order_relaxed);
	if (at >= kLocks || criticalSection == nullptr) {
		return;
	}
	g_locks[at] = Lock{name, static_cast<const CRITICAL_SECTION*>(criticalSection)};
	g_lockCount.store(at + 1, std::memory_order_release);
}

void InstallHangWatch(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (GetPrivateProfileIntA("Probe", "HangWatch", 0, iniPath) != 1) {
		return;
	}
	g_log = log;
	HangWatchLock("FaceGen's model cache 0x00B39C00", reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x00B39C00)));
	HangWatchLock("FaceGen's data 0x00B39C80", reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x00B39C80)));
	HangWatchLock("the texture map 0x00B35380", reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x00B35380)));
	HangWatchLock("the heap 0x00B32B80", reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x00B32B80)));
	if (!AddStepHook(&Step, "the hang watch's step count", log)) {
		log("Probe: hang watch - no step hook; not watching");
		return;
	}
	const HANDLE thread = CreateThread(nullptr, 0, &Watch, nullptr, 0, nullptr);
	if (thread == nullptr) {
		log("Probe: hang watch - no thread; not watching");
		return;
	}
	CloseHandle(thread);
	log("Probe: hang watch - after %u seconds without an update step every thread's place and the known locks' holders are written, at most %u times",
	    hw::kStallMs / 1000, hw::kMaxDumps);
}

}  // namespace obng::plugin
