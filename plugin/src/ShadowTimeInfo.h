#pragma once

#include "Types.h"

namespace obng::plugin {

using LogFn = void (*)(const char* format, ...);

// The range-walk shadow needs two detours: the walk's entry 0x007D59E0 and
// the recording one on AddGeometry 0x007D5790.
constexpr UInt32 kRangeWalkDetours = 2;

// [Shadow] RangeWalkOrder: which side of the range-walk shadow runs first.
// original-first (the default, the gate as it ran): the original walks and
// performs its AddGeometry calls, then BatchedWalk reads the scene as the
// original left it. rebuild-first (a diagnostic): BatchedWalk reads the scene
// untouched, then the original runs - a mismatch here with none under
// original-first shows the original's walk changing its own inputs.
enum class RangeWalkOrder : UInt8 {
	OriginalFirst,
	RebuildFirst,
};

inline const char* RangeWalkOrderName(RangeWalkOrder order) {
	return order == RangeWalkOrder::RebuildFirst ? "rebuild-first" : "original-first";
}

// Case-insensitive, whole word. Anything else gives original-first with
// *known false, so the caller can say the value was not understood.
inline RangeWalkOrder ParseRangeWalkOrder(const char* text, bool* known) {
	const auto equals = [text](const char* word) {
		UInt32 i = 0;
		for (; word[i] != '\0'; ++i) {
			char c = text[i];
			if (c >= 'A' && c <= 'Z') {
				c = static_cast<char>(c - 'A' + 'a');
			}
			if (c != word[i]) {
				return false;
			}
		}
		return text[i] == '\0';
	};
	*known = true;
	if (equals("original-first")) {
		return RangeWalkOrder::OriginalFirst;
	}
	if (equals("rebuild-first")) {
		return RangeWalkOrder::RebuildFirst;
	}
	*known = false;
	return RangeWalkOrder::OriginalFirst;
}

struct ShadowSettings {
	bool update = true;
	bool init = true;
	bool freeze = true;
	bool unfreeze = true;
	bool loadingMenuTick = true;
	bool loadingMenuCtor = true;
	bool rangeWalk = false;      // the shadow-light range walk 0x007D59E0 against BatchedWalk (ShadowRangeWalk.cpp); off in play
	RangeWalkOrder rangeWalkOrder = RangeWalkOrder::OriginalFirst;  // [Shadow] RangeWalkOrder
	bool sqrt = false;           // the CRT square root _CIsqrt 0x00982C30 against the generated dispatch (FastSqrt.cpp); a gate run, off in play
	bool volatilePack = false;   // a volatile geometry's pack that the route would skip, run and compared with the record's last (FastVolatilePack.cpp); a gate run, off in play
	bool gridUpdate = false;     // the cells' selected update 0x007073A0 from TESObjectCELL::UpdateSelectedNodes against the flat selected run (FastSceneUpdate.cpp); a gate run, off in play
	bool actorUpdate = false;    // NiAVObject::Update 0x00707370 from ApplyAnimData against the flat subtree run (FastSceneUpdate.cpp); a gate run, off in play
	bool fpuControlWord = true;  // log the x87 control word at the first Update
	bool havokPlanner = true;    // sample the planner's outputs every frame, histogram in the summary
	bool callCounters = false;   // count the scene pass's hot functions per return site (Probes.cpp); off in play
	bool passSteps = false;      // time nine steps of the scene pass (calls and TSC ticks per summary, Probes.cpp); off in play
	bool packCache = false;      // hash every packed stream at its unlock and count the packs equal to the key's last one (Probes.cpp); off in play
	bool constantUploads = false;  // hash every vertex constant upload and count the ones equal to the register's last (Probes.cpp); off in play
	bool viewConstants = false;    // key the second eye's re-applied uploads by constant-map entry: which depend on the view (Probes.cpp; needs ConstantUploads, which Plugin.cpp turns on with it); off in play
	bool drawEntries = false;      // count the renderer's four other draw entries (slots 85, 90, 91, 93; Probes.cpp); off in play
	bool actorUpdateTiming = false;  // time the original's NiAVObject::Update calls from ApplyAnimData with the route's own clock, for the comparison (FastSceneUpdate.cpp); off in play
	bool sceneUpdate = false;      // count and time Gamebryo's scene graph update outside the pass (eleven detours, Probes.cpp); off in play
	bool occlusion = false;        // sample one eye pass in occlusionEvery: which drawn geometries' bounds lie behind the finished depth buffer (OcclusionProbe.cpp); not beside PassSteps, DrawEntries, ConstantUploads or the stereo route (Plugin.cpp turns it off then); off in play
	UInt32 occlusionEvery = 31;    // [Probe] OcclusionEvery
	bool grass = false;            // per world pass the grass patches under the Grass node, hidden and drawn, by distance, with the camera's heading (GrassProbe.cpp; needs [Route] StereoDraw=1); cheap, may stay on in play
	bool unroutedDraws = false;    // count every device draw by where it falls and keep the sites outside the stereo route (UnroutedDrawProbe.cpp; needs [Route] StereoDraw=1); off in play
	bool heapCalls = false;      // [Probe] HeapCalls: the engine allocator's calls counted and timed on the render thread (HeapProbe.cpp); a measurement
	bool memoryProbe = true;     // commit, working set and address space in the summary
	UInt32 mismatchLines = 20;   // mismatches written in full per function
	UInt32 summaryEverySeconds = 10;
	bool summaryTimer = false;   // a thread drives the summary (runs with no per-frame detour); off in play

	UInt32 Asked() const {
		return (update ? 1u : 0u) + (init ? 1u : 0u) + (freeze ? 1u : 0u) + (unfreeze ? 1u : 0u) +
		       (loadingMenuTick ? 1u : 0u) + (loadingMenuCtor ? 1u : 0u) + (rangeWalk ? kRangeWalkDetours : 0u) + (sqrt ? 1u : 0u) +
		       (actorUpdate ? 1u : 0u) + (gridUpdate ? 1u : 0u) + (volatilePack ? 2u : 0u);
	}
};

// Puts the shadow detours on the TimeInfo functions the settings name:
// each call runs the original on the game's state and the rebuild on a
// copy, compares bit for bit, and the game continues with the original's
// result. Returns how many detours were made; what was skipped is logged.
UInt32 InstallShadowTimeInfo(const ShadowSettings& settings, LogFn log);

// The same for LoadingMenu::Tick and LoadingMenu::LoadingMenu
// (ShadowLoadingMenu.cpp). Call after InstallShadowTimeInfo.
UInt32 InstallShadowLoadingMenu(const ShadowSettings& settings, LogFn log);

// Whether TimeInfo::Update's entry still holds the game's own bytes. When
// another plugin's clock (OBVR's or OblivionFrameFixes' PreciseFrameClock)
// sits there, every shadow whose rebuild contains an Update - the
// LoadingMenu's Tick and constructor - would compare against that plugin,
// not against the game, and must stay off.
bool TimeInfoUpdateEntryIsTheGames();

// The x87 control word logged once, from whichever shadow runs first with
// the probe on (Update is off in play because of OBVR's clock, so Tick,
// which runs every frame, carries the probe there).
void LogFpuControlWordOnce(const char* where);

// What an entry detour is for, as its log lines say it: a shadow compares
// (the original runs), a route replaces (OBNG's rebuild runs instead).
enum class DetourRole : UInt8 { Shadow, Route };

// One entry detour: the bytes checked, the trampoline built, the patch
// written; the original reachable through *original afterwards. Shared by
// the shadow installers and the route.
bool InstallEntryDetour(const char* name, UInt32 address, const UInt8* bytes, UInt32 length, const void* replacement,
                        void** original, LogFn log, DetourRole role = DetourRole::Shadow);

}  // namespace obng::plugin
