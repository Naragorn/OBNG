#include "Lighting30Clip.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "engine/render/Lighting30ClipLogic.h"

namespace obng::plugin {

namespace {

namespace clip = obng::engine::render::lighting30_clip;

using MakerFn = UInt32(__thiscall*)(UInt32 shader, UInt32 path, UInt32 macros, UInt32 profile, const char* name, UInt32 a5, UInt32 a6);
using LookupFn = UInt32(__thiscall*)(UInt32 package, const char* name);

LogFn g_log = nullptr;
bool g_on = false;
clip::Counts g_counts;  // the loader runs on one thread, at the renderer's start

template <typename Fn>
Fn Game(std::uint32_t address) {
	return reinterpret_cast<Fn>(static_cast<std::uintptr_t>(address));
}

UInt32 ValueAt(std::uint32_t address) {
	UInt32 value;
	std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), sizeof(value));
	return value;
}

// The named program's bytecode in the package, its light sum's add given the saturate.
void ClipInPackage(const char* name) {
	const UInt32 package = ValueAt(clip::kPackage);
	const UInt32 entry = package != 0 && name != nullptr ? Game<LookupFn>(clip::kLookup)(package, name) : 0;
	if (entry == 0) {
		clip::Count(g_counts, false, clip::Outcome::kBroken, false);
		return;
	}
	const std::uint32_t* tokens = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(entry) + clip::kEntryCode);
	const clip::Found found = clip::FindLightSum(tokens, clip::TokensOf(ValueAt(entry + clip::kEntryLength)));
	bool written = false;
	if (found.outcome == clip::Outcome::kClip) {
		const std::uint32_t token = clip::WithSaturate(tokens[found.destination]);
		written = SafeWrite(entry + clip::kEntryCode + static_cast<UInt32>(found.destination) * 4, &token, sizeof(token));
	}
	clip::Count(g_counts, true, found.outcome, written);
}

// In place of the loader's calls of the pixel program maker: ECX the shader, six arguments, the name the fourth.
UInt32 __fastcall MakeClipped(UInt32 shader, void*, UInt32 path, UInt32 macros, UInt32 profile, const char* name, UInt32 a5, UInt32 a6) {
	ClipInPackage(name);
	return Game<MakerFn>(clip::kMaker)(shader, path, macros, profile, name, a5, a6);
}

void CallBytes(std::uint32_t site, std::uintptr_t target, UInt8 bytes[5]) {
	const UInt32 relative = static_cast<UInt32>(target) - (site + 5);
	bytes[0] = 0xE8;
	std::memcpy(bytes + 1, &relative, 4);
}

// All three calls or none: a part of the programs clipped would be a third look.
bool RedirectMakerCalls() {
	UInt8 bytes[5];
	for (const std::uint32_t site : clip::kMakerCalls) {
		CallBytes(site, clip::kMaker, bytes);
		if (!Verify(site, bytes, sizeof(bytes))) {
			return false;
		}
	}
	bool written = true;
	for (const std::uint32_t site : clip::kMakerCalls) {
		CallBytes(site, reinterpret_cast<std::uintptr_t>(&MakeClipped), bytes);
		written = SafeWrite(site, bytes, sizeof(bytes)) && written;
	}
	return written;
}

}  // namespace

void InstallLighting30Clip(const char* iniPath, LogFn log, bool kindsOn) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (!clip::On(static_cast<int>(GetPrivateProfileIntA("Probe", "Lighting30Clip", clip::Shipped(), iniPath)), kindsOn)) {
		return;
	}
	g_log = log;
	if (!RedirectMakerCalls()) {
		log("Probe: Lighting30 clip - the loader's calls of the pixel program maker at %08X, %08X, %08X are not the game's own or could not "
		    "all be written; the light sum is not clipped",
		    clip::kMakerCalls[0], clip::kMakerCalls[1], clip::kMakerCalls[2]);
		return;
	}
	g_on = true;
	log("Probe: Lighting30 clip - on at %08X, %08X, %08X: as Lighting30Shader makes its pixel programs, the add that completes ambient plus "
	    "lights gets the saturate in the shader package's bytecode - the light sum clipped at 1 before the texture, as the game's multi-pass "
	    "path clips it. For everything this shader draws, the game's own hair and heads too",
	    clip::kMakerCalls[0], clip::kMakerCalls[1], clip::kMakerCalls[2]);
}

void LogLighting30ClipSummary() {
	if (!g_on) {
		return;
	}
	g_log("Probe: Lighting30 clip - since the start %llu pixel programs made by the shader's loader: %llu clipped, %llu clipped already; left "
	      "as they are: %llu without the constant AmbientColor (no lighting programs), %llu whose sum has another shape, %llu no version 2 or "
	      "3 pixel program, %llu broken, %llu not in the package, %llu found and not written",
	      g_counts.asked, g_counts.clipped, g_counts.already, g_counts.noAmbient, g_counts.noSum, g_counts.noProgram, g_counts.broken,
	      g_counts.noEntry, g_counts.notWritten);
}

}  // namespace obng::plugin
