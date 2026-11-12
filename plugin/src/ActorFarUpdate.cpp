#include "ActorFarUpdate.h"

#include <windows.h>

#include <cmath>
#include <cstring>

#include "OcclusionCull.h"
#include "engine/scene/ActorFarRateLogic.h"
#include "engine/scene/ActorFarUpdateLogic.h"

namespace obng::plugin {

namespace {

namespace far_rate = obng::engine::scene::actor_far_rate;
namespace far_update = obng::engine::scene::actor_far_update;

constexpr const char* kName = "Actor::Update (far actors)";
constexpr UInt32 kAddress = 0x00603CA0;  // __thiscall, one float, RET 4 at 0x00605759 (read)
constexpr UInt8 kBytes[] = {0x6A, 0xFF, 0x68, 0x60, 0x2C, 0x9C, 0x00};  // push -1; push 009C2C60h (read)
constexpr UInt32 kPlayerPointer = 0x00B333C4;  // (read 0x00603CA0's compares; xOBSE g_thePlayer, OBVR kPlayerPointer)
constexpr UInt32 kFrameSeconds = 0x00B33E9C;   // frameSeconds as Actor::Update reads it (read: seven places in 0x00603CA0; knowledge/actor-shadows.md)
constexpr UInt32 kClockMs = 0x00B33EA0;        // TimeInfo 0x00B33E90 +0x10 clockMs: moves once a frame (knowledge/frame-clock.md; xOBSE g_timeInfo)
constexpr UInt32 kRefPosition = 0x2C;          // (OBVR GameAddresses.h kRefPositionOffset)
constexpr UInt32 kActorProcess = 0x58;         // the process (read 0x00659A00: [this+0x58] -> vfunc +0x08 is the level)
constexpr UInt32 kProcessLevelSlot = 0x08;

using UpdateFn = void(__thiscall*)(void* self, float time);
using LevelFn = int(__thiscall*)(void* self);

void* g_original = nullptr;
bool g_installed = false;
LogFn g_log = nullptr;
std::uint32_t g_every = 1;
float g_distance = 6000.0f;
// [Route] ActorViewCount: the near actors' calls by where the camera of the
// last eye pass looks (ActorFarUpdateLogic.h ViewOfActor); a measurement.
bool g_viewCount = false;
// [Route] ActorViewRule: a near actor outside the cone around the main
// camera's direction gets the far actors' rate (ActorFarUpdateLogic.h
// TakenByView). The direction comes from the world scene graph's camera, not
// from a render pass, so that the rule is the same in the flat game and under
// OBVR in either pass mode: OBVR writes the head's rotation into that node's
// transform (OBVR src/camera/CameraHook.cpp, read by the OBVR session
// 2026-11-12), and the engine leaves the frame's camera there.
far_update::ViewRule g_viewRule;
UINT g_viewAlternate = 0;          // [Route] ActorViewAlternate: 1 the view rule, 2 its hidden part only in every second summary window (a measurement)
// [Route] ActorViewHidden: the rule's second part - an actor inside the cone
// whose scene node the occlusion culler left out gets every
// ActorViewHiddenEvery-th call (TakenByViewOrHidden).
bool g_viewHidden = false;
std::uint32_t g_viewHiddenEvery = 4;
volatile LONG g_viewHiddenOnNow = 1;
volatile LONG64 g_viewHiddenCalls = 0;  // calls of actors the hidden part took
volatile LONG64 g_viewHiddenSeen = 0;   // the count: calls of near actors inside the cone whose node the culler left out
constexpr UInt32 kRefNode = 0x3C;       // the reference's scene node (ActorCensus.cpp)
constexpr UInt32 kRefBase = 0x1C;       // its base form, and a form's id (ActorCensus.cpp)
constexpr UInt32 kFormId = 0x0C;
unsigned g_viewHiddenLogLeft = 0;       // with the count: sample lines of actors taken as hidden left in this window
volatile LONG g_viewOnNow = 1;
volatile LONG64 g_viewCalls = 0;   // calls of near actors the rule took
volatile LONG64 g_viewCombat = 0;  // calls it would have taken but for combat
constexpr UInt32 kWorldSceneGraph = 0x00B333CC;      // (re/functions/0x0040C830.md; OBVR GameAddresses.h kWorldSceneGraph)
constexpr UInt32 kSceneGraphCamera = 0xDC;           // (OBVR GameAddresses.h; xOBSE SceneGraph::camera at 0x0DC)
constexpr UInt32 kObjectWorldRotation = 0x64;        // (read 0x0070C147, SceneUpdate.h kWorldTransformOffset; xOBSE NiAVObject::m_worldRotate)
constexpr UInt32 kObjectWorldTranslate = 0x88;       // (the same transform's translate, + 9 floats; xOBSE m_worldTranslate at 0x088)
constexpr UInt32 kActorInCombatSlot = 0x334;         // Actor's slot 0xCD (xOBSE GameObjects.h Actor::IsInCombat(bool)); Character and Creature hold
                                                     // 0x005E6110 there: the process's package of type 0x0C or 0x0D (read in the image)
using InCombatFn = bool(__thiscall*)(void* self, bool unknown);
far_update::ViewCounts g_viewCounts;
LONG64 g_viewNoCamera = 0;
constexpr float kHeadHeight = 110.0f;  // game units above the reference's feet; the count's bands are 1000 units wide
unsigned g_viewLogLeft = 0;            // sample lines left in this summary window
bool g_alternate = false;
DWORD g_thread = 0;                // the thread of the first call: the only one whose calls are left out
volatile LONG g_onNow = 1;         // with the alternation: whether this summary window uses the lower rate
volatile LONG64 g_farCalls = 0;    // calls of far actors
volatile LONG64 g_skipped = 0;     // of them left out
volatile LONG64 g_summed = 0;      // calls run with summed time
// The travel measurement: the far actors' way between their updates that ran
// and the game seconds between them, in thousandths of a unit and microseconds.
volatile LONG64 g_travelUnits = 0;
volatile LONG64 g_travelMicros = 0;
volatile LONG64 g_movingUnits = 0;   // of them, of actors moving faster than far_update::kMovingSpeed
volatile LONG64 g_movingMicros = 0;
// [Route] ActorFarProbe: what time the character controller's update
// 0x00896000 (knowledge/threading.md) is handed inside an update that runs
// with summed time - its argument's first float is its step (read: stored at
// +0x2D8 with its reciprocal at +0x2DC, 0x00896000's `in_ECX[0xb6]`).
constexpr UInt32 kController = 0x00896000;
constexpr UInt8 kControllerBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp, esp; and esp, -10h (read)
using ControllerFn = void(__thiscall*)(void* self, float* step);
void* g_controllerOriginal = nullptr;
bool g_probe = false;
float g_inSummed = 0.0f;                 // the summed seconds of the update in flight; 0: none
bool g_inFar = false;                    // a far actor's update is in flight
// By kind of update in flight (0: a far actor's with the frame's own time, 1: with summed time): the
// controller's updates, the step they were handed, the summed seconds they ran in, and the way they
// were asked to go (the argument's floats 4..6, read: divided by the step into the velocity at +0x290).
volatile LONG64 g_controllerCalls[2] = {};
volatile LONG64 g_controllerMicros[2] = {};
volatile LONG64 g_controllerSummedMicros = 0;
volatile LONG64 g_controllerMilliUnits[2] = {};

void __fastcall Controller(void* self, void*, float* step) {
	if (g_inFar && step != nullptr && GetCurrentThreadId() == g_thread) {
		const int kind = g_inSummed > 0.0f ? 1 : 0;
		const double x = static_cast<double>(step[4]);
		const double y = static_cast<double>(step[5]);
		const double asked = std::sqrt(x * x + y * y);  // the way on the ground
		InterlockedIncrement64(&g_controllerCalls[kind]);
		InterlockedExchangeAdd64(&g_controllerMicros[kind], static_cast<LONG64>(static_cast<double>(*step) * 1000000.0));
		if (asked == asked && asked < 100000.0) {
			InterlockedExchangeAdd64(&g_controllerMilliUnits[kind], static_cast<LONG64>(asked * 1000.0));
		}
		if (kind == 1) {
			InterlockedExchangeAdd64(&g_controllerSummedMicros, static_cast<LONG64>(static_cast<double>(g_inSummed) * 1000000.0));
		}
	}
	reinterpret_cast<ControllerFn>(g_controllerOriginal)(self, step);
}

// The animation's update 0x00476D10 (proposal for the name; __thiscall of the
// animation data with the actor, the step and a time or -1; read: the step is
// added to the time at +0x94 and the controllers run at it): what step a far
// actor's animation gets, inside its update and outside of it.
constexpr UInt32 kAnimUpdate = 0x00476D10;
constexpr UInt8 kAnimUpdateBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};  // push ebp; mov ebp, esp; and esp, -8 (read)
using AnimUpdateFn = void(__thiscall*)(void* self, UInt32 actor, float step, float time);
void* g_animOriginal = nullptr;
volatile LONG64 g_animCalls[3] = {};   // 0, 1 as the controller's kinds; 2: a far actor's, outside its update
volatile LONG64 g_animMicros[3] = {};
volatile LONG64 g_animTicks[3] = {};   // the time they took (QueryPerformanceCounter)

bool IsFar(UInt32 actor, UInt32 player);
bool MainCamera(float eye[3], float direction[3]);
bool InCombat(UInt32 actor);
UInt32 Dword(UInt32 address);

far_update::Clock g_clock;
far_update::Table g_table;
bool g_rootMotion = true;              // [Route] ActorFarRootMotion: the animation's steps summed for the update that runs
volatile LONG64 g_rootTaken = 0;       // updates run with summed time that got the summed step
volatile LONG64 g_rootMissing = 0;     // ... that had none to get
constexpr UInt32 kAnimRootMotion = 0x0C;  // the animation data's step of its last update, three floats (read 0x00476D10, 0x004723A0)

void __fastcall AnimUpdate(void* self, void*, UInt32 actor, float step, float time) {
	const bool ours = GetCurrentThreadId() == g_thread;
	const UInt32 player = ours ? Dword(kPlayerPointer) : 0;
	int timedKind = -1;
	if (ours && g_probe) {
		int kind = -1;
		if (g_inFar) {
			kind = g_inSummed > 0.0f ? 1 : 0;
		} else if (actor != 0 && player != 0 && actor != player && IsFar(actor, player)) {
			kind = 2;
		}
		if (kind >= 0 && step == step) {
			timedKind = kind;
			InterlockedIncrement64(&g_animCalls[kind]);
			InterlockedExchangeAdd64(&g_animMicros[kind], static_cast<LONG64>(static_cast<double>(step) * 1000000.0));
		}
	}
	if (timedKind >= 0) {
		LARGE_INTEGER before;
		LARGE_INTEGER after;
		QueryPerformanceCounter(&before);
		reinterpret_cast<AnimUpdateFn>(g_animOriginal)(self, actor, step, time);
		QueryPerformanceCounter(&after);
		InterlockedExchangeAdd64(&g_animTicks[timedKind], after.QuadPart - before.QuadPart);
	} else {
		reinterpret_cast<AnimUpdateFn>(g_animOriginal)(self, actor, step, time);
	}
	if (ours && g_rootMotion && actor != 0 && actor != player) {
		if (far_update::Entry* entry = g_table.Peek(actor)) {
			float made[3];
			std::memcpy(made, reinterpret_cast<const void*>(reinterpret_cast<UInt32>(self) + kAnimRootMotion), sizeof(made));
			far_update::AddRootMotion(*entry, reinterpret_cast<UInt32>(self), made, g_clock.frame);
		}
	}
}

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }

void Original(void* self, float time) { reinterpret_cast<UpdateFn>(g_original)(self, time); }

// Whether the actor stands beyond the distance from the player. A position
// that is not a number compares false: near.
bool IsFar(UInt32 actor, UInt32 player) {
	float here[3];
	float there[3];
	std::memcpy(here, reinterpret_cast<const void*>(actor + kRefPosition), sizeof(here));
	std::memcpy(there, reinterpret_cast<const void*>(player + kRefPosition), sizeof(there));
	const float x = here[0] - there[0];
	const float y = here[1] - there[1];
	const float z = here[2] - there[2];
	return x * x + y * y + z * z > g_distance * g_distance;
}

// The world scene graph's camera: where it is and where it looks. A Gamebryo
// camera looks along its rotation's first column (measured against the eye
// pass's direction, see the view samples).
bool MainCamera(float eye[3], float direction[3]) {
	const UInt32 sceneGraph = Dword(kWorldSceneGraph);
	const UInt32 camera = sceneGraph != 0 ? Dword(sceneGraph + kSceneGraphCamera) : 0;
	if (camera == 0) {
		return false;
	}
	float rotation[9];
	std::memcpy(rotation, reinterpret_cast<const void*>(camera + kObjectWorldRotation), sizeof(rotation));
	std::memcpy(eye, reinterpret_cast<const void*>(camera + kObjectWorldTranslate), 3 * sizeof(float));
	direction[0] = rotation[0];
	direction[1] = rotation[3];
	direction[2] = rotation[6];
	return true;
}

bool InCombat(UInt32 actor) {
	return reinterpret_cast<InCombatFn>(Dword(Dword(actor) + kActorInCombatSlot))(reinterpret_cast<void*>(actor), false);
}

// An update that runs: the way the actor made since its last one that ran,
// counted when it is far, and where it stands now.
void NoteTravel(far_update::Entry& entry, UInt32 actor, bool counted, double seconds) {
	float here[3];
	std::memcpy(here, reinterpret_cast<const void*>(actor + kRefPosition), sizeof(here));
	if (counted && entry.hasPosition) {
		const double x = static_cast<double>(here[0]) - static_cast<double>(entry.lastPosition[0]);
		const double y = static_cast<double>(here[1]) - static_cast<double>(entry.lastPosition[1]);
		const double z = static_cast<double>(here[2]) - static_cast<double>(entry.lastPosition[2]);
		const double units = std::sqrt(x * x + y * y + z * z);
		const far_update::Travel travel = far_update::TravelOf(units, seconds);
		if (travel != far_update::Travel::None) {
			const LONG64 milliUnits = static_cast<LONG64>(units * 1000.0);
			const LONG64 micros = static_cast<LONG64>(seconds * 1000000.0);
			InterlockedExchangeAdd64(&g_travelUnits, milliUnits);
			InterlockedExchangeAdd64(&g_travelMicros, micros);
			if (travel == far_update::Travel::Moving) {
				InterlockedExchangeAdd64(&g_movingUnits, milliUnits);
				InterlockedExchangeAdd64(&g_movingMicros, micros);
			}
		}
	}
	std::memcpy(entry.lastPosition, here, sizeof(here));
	entry.hasPosition = true;
}

// The entry's replacement: __fastcall with an unused second register takes a
// __thiscall's this and cleans the one stack argument as the original does.
void __fastcall Update(void* self, void*, float time) {
	const UInt32 actor = reinterpret_cast<UInt32>(self);
	const UInt32 player = Dword(kPlayerPointer);
	const DWORD thread = GetCurrentThreadId();
	if (g_thread == 0) {
		g_thread = thread;
	}
	auto* frameSeconds = reinterpret_cast<float*>(kFrameSeconds);
	if (thread != g_thread || player == 0) {
		Original(self, time);
		return;
	}
	g_clock.See(Dword(kClockMs), *frameSeconds);
	if (actor == player) {
		Original(self, time);
		return;
	}
	// The high loop's call only: time 0 and the high process (level 0). The
	// lower levels have their own intervals (0x00674200, 0x00673E90).
	const UInt32 process = Dword(actor + kActorProcess);
	if (time != 0.0f || process == 0 ||
	    reinterpret_cast<LevelFn>(Dword(Dword(process) + kProcessLevelSlot))(reinterpret_cast<void*>(process)) != 0) {
		Original(self, time);
		return;
	}
	std::uint32_t every = 1;
	const bool isFar = IsFar(actor, player);
	if (isFar) {
		InterlockedIncrement64(&g_farCalls);
		every = g_onNow != 0 ? g_every : 1;
	}
	bool taken = false;
	if ((g_viewCount || g_viewRule.on) && !isFar) {
		float eye[3];
		float direction[3];
		const bool known = MainCamera(eye, direction);
		float here[3];
		std::memcpy(here, reinterpret_cast<const void*>(actor + kRefPosition), sizeof(here));
		here[2] += kHeadHeight;
		if (g_viewCount) {
			if (known) {
				g_viewCounts.Add(far_update::ViewOfActor(here, eye, direction, g_viewRule.coneCosine));
			} else {
				++g_viewNoCamera;
			}
			if (g_viewLogLeft != 0) {
				--g_viewLogLeft;
				float player3[3];
				std::memcpy(player3, reinterpret_cast<const void*>(player + kRefPosition), sizeof(player3));
				float passCamera[3] = {};
				float passDirection[3] = {};
				const bool pass = OcclusionCullLastCamera(passCamera, passDirection);
				g_log("Route: near actors' view sample - main camera %s at %.0f %.0f %.0f looking %.3f %.3f %.3f; the player at %.0f %.0f %.0f; the "
				      "last eye pass %s looking %.3f %.3f %.3f; actor at %.0f %.0f %.0f",
				      known ? "known" : "NOT known", static_cast<double>(eye[0]), static_cast<double>(eye[1]), static_cast<double>(eye[2]),
				      static_cast<double>(direction[0]), static_cast<double>(direction[1]), static_cast<double>(direction[2]),
				      static_cast<double>(player3[0]), static_cast<double>(player3[1]), static_cast<double>(player3[2]), pass ? "known" : "not known",
				      static_cast<double>(passDirection[0]), static_cast<double>(passDirection[1]), static_cast<double>(passDirection[2]),
				      static_cast<double>(here[0]), static_cast<double>(here[1]), static_cast<double>(here[2]));
			}
		}
		const bool leftOut = (g_viewHidden || g_viewCount) && OcclusionCullNodeLeftOut(Dword(actor + kRefNode));
		if (g_viewCount && leftOut && known && far_update::ViewOfActor(here, eye, direction, g_viewRule.coneCosine).side == far_update::ViewSide::Inside) {
			InterlockedIncrement64(&g_viewHiddenSeen);
		}
		// Combat is asked only for an actor the rule would take: one virtual call.
		const far_update::Taken by = g_viewOnNow != 0 ? far_update::TakenByViewOrHidden(g_viewRule, g_viewHidden && g_viewHiddenOnNow != 0, known, false,
		                                                                               leftOut, here, eye, direction)
		                                              : far_update::Taken::No;
		if (by != far_update::Taken::No) {
			if (InCombat(actor)) {
				InterlockedIncrement64(&g_viewCombat);
			} else if (by == far_update::Taken::OutsideCone) {
				taken = true;
				InterlockedIncrement64(&g_viewCalls);
				every = g_onNow != 0 ? g_every : 1;
			} else {
				taken = true;
				InterlockedIncrement64(&g_viewHiddenCalls);
				every = g_viewHiddenEvery;
				if (g_viewCount && g_viewHiddenLogLeft != 0) {
					--g_viewHiddenLogLeft;
					const UInt32 base = Dword(actor + kRefBase);
					g_log("Route: near actors' hidden sample - actor %08X (form %08X, base form %08X) node %08X at %.0f %.0f %.0f taken as hidden",
					      actor, Dword(actor + kFormId), base != 0 ? Dword(base + kFormId) : 0, Dword(actor + kRefNode), static_cast<double>(here[0]),
					      static_cast<double>(here[1]), static_cast<double>(here[2]));
				}
			}
		}
	}
	const bool lower = isFar || taken;
	bool isNew = false;
	far_update::Entry& entry = g_table.Find(actor, g_clock.frame, isNew);
	const double since = g_clock.seconds - entry.lastSeconds;
	const far_update::Step step = far_update::Decide(entry, isNew, every, g_clock, *frameSeconds);
	if (!step.runs) {
		InterlockedIncrement64(&g_skipped);
		return;
	}
	NoteTravel(entry, actor, lower && !isNew, since);
	if (!step.summed) {
		far_update::DropRootMotion(entry);
		g_inFar = lower;
		Original(self, time);
		g_inFar = false;
		return;
	}
	InterlockedIncrement64(&g_summed);
	// The way to go: the animation's steps of the frames this update stands for.
	const UInt32 animData = far_update::FreshAnimData(entry, g_clock.frame);
	float way[3];
	if (far_update::TakeRootMotion(entry, animData, way)) {
		std::memcpy(reinterpret_cast<void*>(animData + kAnimRootMotion), way, sizeof(way));
		InterlockedIncrement64(&g_rootTaken);
	} else {
		InterlockedIncrement64(&g_rootMissing);
	}
	const float frame = *frameSeconds;
	*frameSeconds = step.seconds;
	g_inSummed = step.seconds;
	g_inFar = true;
	Original(self, time);
	g_inFar = false;
	g_inSummed = 0.0f;
	*frameSeconds = frame;
}

}  // namespace

void InstallActorFarUpdate(const char* iniPath, LogFn log) {
	if (g_installed || GetPrivateProfileIntA("Route", "ActorFarWhole", 0, iniPath) == 0) {
		return;
	}
	const UINT every = GetPrivateProfileIntA("Route", "ActorFarEvery", 1, iniPath);
	if (every <= 1) {
		log("Route %s: ActorFarWhole=1 with ActorFarEvery=1; off", kName);
		return;
	}
	g_log = log;
	g_every = every > 64 ? 64 : every;
	g_distance = static_cast<float>(GetPrivateProfileIntA("Route", "ActorFarDistance", 6000, iniPath));
	g_alternate = GetPrivateProfileIntA("Route", "ActorFarAlternate", 0, iniPath) != 0;
	g_viewCount = GetPrivateProfileIntA("Route", "ActorViewCount", 0, iniPath) != 0;
	g_viewRule.on = GetPrivateProfileIntA("Route", "ActorViewRule", 0, iniPath) != 0;
	{
		const UINT degrees = GetPrivateProfileIntA("Route", "ActorViewConeDegrees", 75, iniPath);
		g_viewRule.coneCosine = degrees >= 90 ? 0.0f : static_cast<float>(std::cos(static_cast<double>(degrees) * 3.14159265358979323846 / 180.0));
		g_viewRule.minDistance = static_cast<float>(GetPrivateProfileIntA("Route", "ActorViewMinDistance", 1000, iniPath));
	}
	g_viewAlternate = GetPrivateProfileIntA("Route", "ActorViewAlternate", 0, iniPath);
	g_viewHidden = g_viewRule.on && GetPrivateProfileIntA("Route", "ActorViewHidden", 0, iniPath) != 0;
	{
		const UINT hiddenEvery = GetPrivateProfileIntA("Route", "ActorViewHiddenEvery", 4, iniPath);
		g_viewHiddenEvery = hiddenEvery < 1 ? 1 : hiddenEvery > 64 ? 64 : hiddenEvery;
	}
	if (!InstallEntryDetour(kName, kAddress, kBytes, sizeof(kBytes), reinterpret_cast<const void*>(&Update), &g_original, log, DetourRole::Route)) {
		return;
	}
	g_installed = true;
	g_rootMotion = GetPrivateProfileIntA("Route", "ActorFarRootMotion", 1, iniPath) != 0;
	const bool probe = GetPrivateProfileIntA("Route", "ActorFarProbe", 0, iniPath) != 0;
	if (g_rootMotion || probe) {
		const bool anim = InstallEntryDetour("animation update 0x00476D10 (far actors: the steps summed)", kAnimUpdate, kAnimUpdateBytes, sizeof(kAnimUpdateBytes),
		                                     reinterpret_cast<const void*>(&AnimUpdate), &g_animOriginal, log, DetourRole::Route);
		g_rootMotion = g_rootMotion && anim;
	}
	log("Route %s: the animation's steps of the frames left out %s", kName, g_rootMotion ? "are summed for the update that runs" : "are NOT summed (the actor goes one frame's way an update)");
	if (probe) {
		g_probe = InstallEntryDetour("character controller update 0x00896000 (far actors' probe)", kController, kControllerBytes, sizeof(kControllerBytes),
		                             reinterpret_cast<const void*>(&Controller), &g_controllerOriginal, log, DetourRole::Route);
	}
	if (g_viewRule.on) {
		log("Route %s: the view rule is on - a near actor outside a cone of cosine %.4f around the main camera's direction, from %.0f units, and not "
		    "in combat, gets the same rate%s",
		    kName, static_cast<double>(g_viewRule.coneCosine), static_cast<double>(g_viewRule.minDistance),
		    g_viewAlternate == 1 ? "; alternating: only in every second summary window" : "");
		if (g_viewHidden) {
			log("Route %s: the view rule's hidden part is on - a near actor inside the cone whose scene node the occlusion culler left out gets every "
			    "%u. call (only where the culler's node cull runs)%s",
			    kName, g_viewHiddenEvery, g_viewAlternate == 2 ? "; alternating: only in every second summary window" : "");
		}
	}
	log("Route %s: the whole update of an actor beyond %.0f units on every %u. call, with the summed time%s", kName,
	    static_cast<double>(g_distance), g_every, g_alternate ? "; alternating: only in every second summary window" : "");
}

void LogActorFarUpdateSummary() {
	if (!g_installed) {
		return;
	}
	const LONG64 farCalls = InterlockedExchange64(&g_farCalls, 0);
	const LONG64 skipped = InterlockedExchange64(&g_skipped, 0);
	const LONG64 summed = InterlockedExchange64(&g_summed, 0);
	const bool wasOn = g_onNow != 0;
	if (g_alternate) {
		InterlockedExchange(&g_onNow, wasOn ? 0 : 1);
	}
	g_log("Route: far actors' whole update (every %u. call beyond %.0f units, %s in this window) - %lld calls of far actors, %lld left out, %lld run with summed time",
	      g_every, static_cast<double>(g_distance), wasOn ? "on" : "off", farCalls, skipped, summed);
	const double travelUnits = static_cast<double>(InterlockedExchange64(&g_travelUnits, 0)) / 1000.0;
	const double travelSeconds = static_cast<double>(InterlockedExchange64(&g_travelMicros, 0)) / 1000000.0;
	const double movingUnits = static_cast<double>(InterlockedExchange64(&g_movingUnits, 0)) / 1000.0;
	const double movingSeconds = static_cast<double>(InterlockedExchange64(&g_movingMicros, 0)) / 1000000.0;
	g_log("Route: far actors' travel (%s in this window) - %.1f units in %.1f actor seconds (%.2f units/s); moving ones %.1f units in %.1f actor seconds (%.2f units/s)",
	      wasOn ? "on" : "off", travelUnits, travelSeconds, travelSeconds > 0.0 ? travelUnits / travelSeconds : 0.0, movingUnits, movingSeconds,
	      movingSeconds > 0.0 ? movingUnits / movingSeconds : 0.0);
	{
		const LONG64 taken = InterlockedExchange64(&g_rootTaken, 0);
		const LONG64 missing = InterlockedExchange64(&g_rootMissing, 0);
		g_log("Route: far actors' way - %lld updates run with summed time got the animation's summed step, %lld had none", taken, missing);
	}
	if (g_viewRule.on) {
		const bool viewWasOn = g_viewOnNow != 0;
		const bool hiddenWasOn = g_viewHidden && g_viewHiddenOnNow != 0;
		if (g_viewAlternate == 1) {
			InterlockedExchange(&g_viewOnNow, viewWasOn ? 0 : 1);
		} else if (g_viewAlternate == 2) {
			InterlockedExchange(&g_viewHiddenOnNow, g_viewHiddenOnNow != 0 ? 0 : 1);
		}
		g_log("Route: near actors' view rule (outside a cone of cosine %.4f, from %.0f units, %s in this window) - %lld calls at the far actors' rate, "
		      "%lld kept at the full rate for combat; hidden part (every %u. call, %s in this window) - %lld calls of actors the culler left out",
		      static_cast<double>(g_viewRule.coneCosine), static_cast<double>(g_viewRule.minDistance), viewWasOn ? "on" : "off",
		      InterlockedExchange64(&g_viewCalls, 0), InterlockedExchange64(&g_viewCombat, 0), g_viewHiddenEvery, hiddenWasOn ? "on" : "off",
		      InterlockedExchange64(&g_viewHiddenCalls, 0));
	}
	if (g_viewCount) {
		g_viewLogLeft = 6;  // six sample lines a window
		g_viewHiddenLogLeft = 12;
		const far_update::ViewCounts c = g_viewCounts;
		g_viewCounts = far_update::ViewCounts{};
		const LONG64 noCamera = g_viewNoCamera;
		g_viewNoCamera = 0;
		g_log("Route: near actors by the camera's direction (cone cosine %.4f; bands to 1000, 3000 units and beyond) - %llu calls; inside the cone %llu / "
		      "%llu / %llu; in front outside it %llu / %llu / %llu; behind %llu / %llu / %llu; %lld calls without a camera",
		      static_cast<double>(g_viewRule.coneCosine), c.All(), c.inside[0], c.inside[1], c.inside[2], c.outside[0], c.outside[1], c.outside[2], c.behind[0],
		      c.behind[1], c.behind[2], noCamera);
		g_log("Route: near actors inside the cone whose node the culler left out - %lld calls", InterlockedExchange64(&g_viewHiddenSeen, 0));
	}
	if (g_probe) {
		const double stoodFor = static_cast<double>(InterlockedExchange64(&g_controllerSummedMicros, 0)) / 1000000.0;
		static const char* const kAnimKinds[3] = {"inside updates with the frame's own time", "inside updates run with summed time", "outside the actor's update"};
		for (int kind = 0; kind < 3; ++kind) {
			const LONG64 calls = InterlockedExchange64(&g_animCalls[kind], 0);
			LARGE_INTEGER frequency;
			QueryPerformanceFrequency(&frequency);
			const double ms = static_cast<double>(InterlockedExchange64(&g_animTicks[kind], 0)) * 1000.0 / static_cast<double>(frequency.QuadPart);
			g_log("Route: far actors' animation (%s) - %lld animation updates, handed %.3f s, they took %.3f ms (%.1f us each)", kAnimKinds[kind], calls,
			      static_cast<double>(InterlockedExchange64(&g_animMicros[kind], 0)) / 1000000.0, ms, calls > 0 ? ms * 1000.0 / static_cast<double>(calls) : 0.0);
		}
		for (int kind = 0; kind < 2; ++kind) {
			const LONG64 calls = InterlockedExchange64(&g_controllerCalls[kind], 0);
			const double given = static_cast<double>(InterlockedExchange64(&g_controllerMicros[kind], 0)) / 1000000.0;
			const double asked = static_cast<double>(InterlockedExchange64(&g_controllerMilliUnits[kind], 0)) / 1000.0;
			g_log("Route: far actors' controller (%s) - %lld controller updates were handed %.3f s and asked to go %.1f units on the ground (%.2f units/s)",
			      kind == 1 ? "updates run with summed time" : "updates with the frame's own time", calls, given, asked,
			      given > 0.0 ? asked / given : 0.0);
			if (kind == 1) {
				g_log("Route: far actors' controller - the updates run with summed time stood for %.3f s", stoodFor);
			}
		}
	}
}

}  // namespace obng::plugin
