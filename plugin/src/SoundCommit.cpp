#include "SoundCommit.h"

#include <windows.h>

#include <cstring>

#include "Memory.h"
#include "SoundCommitLogic.h"
#include "Types.h"

namespace obng::plugin {

namespace {

namespace sc = obng::plugin::sound_commit;

// Read 2026-11-14 (knowledge/voice-lines.md): in the sound update 0x006AE860, ESI the sound manager, both sites
// load the listener [ESI+0x78], its vtable's +0x44 (CommitDeferredSettings) and call it with the listener pushed.
// EAX, ECX and EDX are loaded anew after both (0x006AEB05 mov ecx,esi; 0x006AF5C1 mov ecx,[esi+0x320]).
constexpr UInt32 kFirstSite = 0x006AEAFA;
constexpr UInt8 kFirstBytes[] = {0x8B, 0x46, 0x78, 0x8B, 0x08, 0x8B, 0x51, 0x44, 0x50, 0xFF, 0xD2};
constexpr UInt32 kSecondSite = 0x006AF5B6;
constexpr UInt8 kSecondBytes[] = {0x8B, 0x46, 0x78, 0x8B, 0x10, 0x50, 0x8B, 0x42, 0x44, 0xFF, 0xD0};
constexpr UInt32 kSiteLength = 11;
constexpr UInt32 kListenerOffset = 0x78;
constexpr UInt32 kSlotCommit = 0x44;

using CommitFn = long(__stdcall*)(UInt32 listener);

sc::State g_state;
sc::Counts g_last;
LogFn g_log = nullptr;
bool g_installed = false;
double g_ticksPerMs = 0.0;

void Commit(UInt32 manager) {
	const UInt32 listener = *reinterpret_cast<const UInt32*>(manager + kListenerOffset);
	const UInt32 vtable = *reinterpret_cast<const UInt32*>(listener);
	LARGE_INTEGER from;
	LARGE_INTEGER to;
	QueryPerformanceCounter(&from);
	reinterpret_cast<CommitFn>(*reinterpret_cast<const UInt32*>(vtable + kSlotCommit))(listener);
	QueryPerformanceCounter(&to);
	g_state.Took(static_cast<unsigned long long>(to.QuadPart - from.QuadPart));
}

void __fastcall First(UInt32 manager) {
	if (g_state.First()) {
		Commit(manager);
	}
}

void __fastcall Second(UInt32 manager) {
	if (g_state.Second()) {
		Commit(manager);
	}
}

// mov ecx, esi; call replacement; nops - in place of the site's eleven bytes.
bool Patch(UInt32 site, void (__fastcall* replacement)(UInt32)) {
	UInt8 patch[kSiteLength];
	std::memset(patch, 0x90, sizeof(patch));
	patch[0] = 0x8B;
	patch[1] = 0xCE;
	patch[2] = 0xE8;
	const UInt32 rel = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(replacement)) - (site + 7);
	std::memcpy(patch + 3, &rel, 4);
	return SafeWrite(site, patch, sizeof(patch));
}

}  // namespace

void InstallSoundCommit(const char* iniPath, LogFn log) {
	const sc::Rule rule = iniPath != nullptr ? sc::RuleFromIni(static_cast<int>(GetPrivateProfileIntA("Route", "SoundCommit", 0, iniPath))) : sc::Rule::kOff;
	if (g_installed || rule == sc::Rule::kOff) {
		return;
	}
	if (!Verify(kFirstSite, kFirstBytes, kSiteLength) || !Verify(kSecondSite, kSecondBytes, kSiteLength)) {
		char first[32];
		char second[32];
		DescribeBytes(kFirstSite, first, sizeof(first));
		DescribeBytes(kSecondSite, second, sizeof(second));
		log("Route sound commit: the sites at %08X and %08X read %s and %s, not the game's own bytes; the sound update stays as it is", kFirstSite,
		    kSecondSite, first, second);
		return;
	}
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	g_ticksPerMs = static_cast<double>(frequency.QuadPart) / 1000.0;
	g_state.Set(rule);
	g_log = log;
	// The second site first: with only the first patched under "once" no commit would be left.
	if (!Patch(kSecondSite, &Second)) {
		log("Route sound commit: the site at %08X could not be patched; the sound update stays as it is", kSecondSite);
		return;
	}
	if (!Patch(kFirstSite, &First)) {
		SafeWrite(kSecondSite, kSecondBytes, kSiteLength);
		log("Route sound commit: the site at %08X could not be patched; the sound update stays as it is", kFirstSite);
		return;
	}
	g_installed = true;
	log("Route sound commit: on (%s) - the sound update's two commits of deferred 3D settings at %08X and %08X", sc::RuleName(rule), kFirstSite,
	    kSecondSite);
}

void LogSoundCommitSummary() {
	if (!g_installed || g_log == nullptr) {
		return;
	}
	const sc::Counts now = g_state.Total();  // read while the main thread counts: a window may be one commit apart
	char line[400];
	sc::FormatSummary(sc::Window(now, g_last), g_state.Now(), g_ticksPerMs, line, sizeof(line));
	g_last = now;
	g_state.NextWindow();  // [Route] SoundCommit=4; the main thread may be between its two sites: one frame's commits count to either window
	g_log("%s", line);
}

}  // namespace obng::plugin
