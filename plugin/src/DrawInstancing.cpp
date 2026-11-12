#include "DrawInstancing.h"

#include <windows.h>

#include <intrin.h>

#include <cstring>

#include "DrawInstancingLogic.h"
#include "EntryDetour.h"
#include "GpuPassTimer.h"
#include "Memory.h"
#include "OcclusionProbe.h"

namespace obng::plugin {

namespace {

namespace di = draw_instancing;

// BSShaderAccumulator::RenderPassList 0x007AC9A0, __thiscall(pass id, mode),
// RET 8 (re/functions/0x007AC9A0.md): its lists at this+0x104+0x14*id, a
// list's head node at +0x04, a node's next at +0x00 and item at +0x08.
constexpr UInt32 kRenderPassList = 0x007AC9A0;
constexpr UInt8 kRenderPassListBytes[] = {0x83, 0xEC, 0x0C, 0x80, 0x3D, 0xE8, 0x2C, 0xB4, 0x00, 0x00};  // sub esp,0Ch; cmp byte [0x00B42CE8],0 (objdump 2026-11-12)
constexpr UInt32 kLists = 0x104;
constexpr UInt32 kListStride = 0x14;
constexpr UInt32 kListHead = 0x04;
constexpr UInt32 kListCount = 0x10;
constexpr UInt32 kNodeNext = 0x00;
constexpr UInt32 kNodeItem = 0x08;
constexpr UInt32 kWalkLookahead = 256;  // nodes the culler may have left out between two records that reach the site

// DrawRenderPass 0x007A9820 (re/functions/0x007A9820.md): its entry bytes, for
// a trampoline of its own, and the step after it published its pass id and
// record, where a record that waits leaves it.
constexpr UInt32 kDrawRenderPass = 0x007A9820;
constexpr UInt8 kDrawRenderPassBytes[] = {0x53, 0x66, 0x8B, 0x5C, 0x24, 0x0C};  // push ebx; mov bx,[esp+0Ch]
// The record step: 0x007A983B `mov [0x00B42EB8],esi` (6 bytes; nothing jumps
// into 0x007A9836..0x007A9840, objdump 2026-11-12), after which ESI is the
// record, EBX the pass id, and the four saved registers on the stack are the
// ones the epilogue 0x007A98D0 pops (pop edi, esi, ebp, ebx; ret 8).
constexpr UInt32 kRecordStep = 0x007A983B;
constexpr UInt8 kRecordStepBytes[] = {0x89, 0x35, 0xB8, 0x2E, 0xB4, 0x00};
UInt32 g_recordStepContinue = 0x007A9841;  // the property lookup, the light constants and the draw
UInt32 g_recordStepReturn = 0x007A98D0;    // the epilogue
constexpr UInt32 kPassIdGlobal = 0x00B42E90;     // published by DrawRenderPass before the record step
constexpr UInt32 kPassRecordGlobal = 0x00B42EB8;
constexpr UInt32 kOriginGlobal = 0x00B3F92C;  // subtracted from every translation the engine packs (0x00761AE0)
constexpr UInt32 kClockMs = 0x00B33EA0;       // TimeInfo's clock: moves once a frame (knowledge/frame-clock.md)

// The record (0x007A9820.md): its geometry at +0x00, its lights' count (a
// byte) at +0x08 and array at +0x0C. The geometry (xOBSE NiObjects.h
// NiAVObject / NiGeometry): world rotation +0x64, translation +0x88, scale
// +0x94, property state +0xAC, effect state +0xB0, model data +0xB4, skin
// +0xB8, shader +0xBC.
constexpr UInt32 kRecordFlags = 0x04;  // pass type (word), +0x06, and +0x07: 0 turns the pixel program's fog off (Toggles.y, 0x007C9F30's end; read in the pseudocode)
constexpr UInt32 kRecordLightCount = 0x08;
constexpr UInt32 kRecordLights = 0x0C;
constexpr UInt32 kMaxRecordLights = 16;
constexpr UInt32 kGeometryRotation = 0x64;
constexpr UInt32 kGeometryTranslation = 0x88;
constexpr UInt32 kGeometryScale = 0x94;
constexpr UInt32 kGeometryProperties = 0xAC;
constexpr UInt32 kGeometryEffects = 0xB0;
constexpr UInt32 kGeometryData = 0xB4;
constexpr UInt32 kGeometrySkin = 0xB8;
constexpr UInt32 kGeometryShader = 0xBC;
// The property state's ten slots after its reference count (xOBSE NiNodes.h);
// a property's flags at +0x18 (NiAlphaProperty, NiZBufferProperty); the
// shader property's flags +0x1C and alpha +0x20, a lighting property's light
// scale +0x94 (DrawRenderPass hands it to every light write); the material's
// values +0x1C..+0x53 with the alpha at +0x50 (NiMaterialProperty); the
// effect state's five words after its flag byte (NiDynamicEffectState).
constexpr UInt32 kPropertySlots = 0x08;
constexpr UInt32 kPropertyFlags = 0x18;
constexpr UInt32 kStencilRef = 0x1C;   // NiStencilProperty's reference and mask (xOBSE)
constexpr UInt32 kStencilMask = 0x20;
constexpr UInt32 kShaderFlags = 0x1C;
constexpr UInt32 kShaderAlpha = 0x20;
constexpr UInt32 kLightScale = 0x94;
constexpr UInt32 kShaderPassCount = 0x34;  // BSShaderProperty's passes: NiTPointerList at +0x28 (xOBSE), its count at +0x0C in it (as the lights' list +0x6C / +0x78)
constexpr UInt32 kMaterialValues = 0x1C;
constexpr UInt32 kMaterialWords = 14;
constexpr UInt32 kMaterialAlpha = 0x50;
constexpr UInt32 kEffectWords = 0x0C;
constexpr UInt32 kLightingPropertyVtables[] = {0x00A92524, 0x00A91384};  // BSShaderLightingProperty, BSShaderPPLightingProperty
constexpr UInt32 kShaderPassObject = 0x3C;  // what slot 12 sets up (re/export/rea-all 0x77a1b0.c)
constexpr std::uint32_t kLearnedAfter = 8;  // a key seen this often the same way is not looked at again

using RenderPassListFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 pass, UInt32 mode);
using DrawRenderPassFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 record, UInt32 pass);

LogFn g_log = nullptr;
di::Mode g_mode = di::Mode::kOff;
UInt32 g_alternate = 0;  // [Route] DrawInstancingAlternate: 1 on and off by summary window, 2 by frame (the lists' time split by the two)
bool g_onNow = true;
RenderPassListFn g_renderPassList = nullptr;
DrawRenderPassFn g_drawRecord = nullptr;  // DrawRenderPass's own body

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const volatile UInt32*>(address); }
float Float(UInt32 address) { return *reinterpret_cast<const volatile float*>(address); }
UInt16 Word(UInt32 address) { return *reinterpret_cast<const volatile UInt16*>(address); }

// The pass list being drawn (render thread). Records wait only in the
// outermost one; a list begun inside a draw draws at once.
struct List {
	UInt32 depth = 0;
	UInt32 accumulator = 0;
	UInt32 head = 0;    // the list's first node
	UInt32 cursor = 0;  // the node after the last record found
};
List g_list;

di::Pending g_pending;
di::Predictions g_predictions;
di::Counts g_counts;
di::VerifyCounts g_verify;
di::Splits g_splits;
std::uint32_t g_ignored = 0;     // [Route] DrawInstancingIgnore: key words left out
bool g_multiPass = false;        // [Route] DrawInstancingMultiPass: geometries with more render passes may wait too (for the comparison)
std::uint32_t g_lightsMode = 2;  // [Route] DrawInstancingLights: 2 all of the record's lights in the key, 1 the first, 0 none
di::InstanceData g_rows[di::Pending::kMaxRecords];  // one flush's instance stream
bool g_replaying = false;  // a flush draws through DrawRenderPass again
UInt32 g_lastClock = 0;
volatile LONG g_summaryRequested = 0;
unsigned __int64 g_summaryTsc = 0;
LARGE_INTEGER g_summaryQpc{};
UInt32 g_mismatchLines = 0;
constexpr UInt32 kMismatchLines = 20;

// The record drawn at once now, for learning its prediction.
struct Drawing {
	UInt32 geometry = 0;
	di::PredictKey predict;
	bool learnable = false;
	bool learned = false;
};
Drawing g_drawing;

// A group's leader drawn instanced.
struct Emission {
	bool on = false;
	UInt32 geometry = 0;
	UInt32 passObject = 0;
	di::InstanceRows rows{};  // the leader's
	std::uint32_t first = 0;
	std::uint32_t count = 0;
	bool checked = false;
	bool used = false;
	bool failed = false;
	di::Fallback why = di::Fallback::kNotDrawn;
};
Emission g_emission;

// Mode 1: the record whose device state is read at its first device draw.
struct Verifying {
	bool on = false;
	bool leader = false;
	bool done = false;
	bool haveLeader = false;
	UInt32 geometry = 0;
	UInt32 record = 0;
	UInt32 pass = 0;
	float leaderScale = 0.0f;
	di::InstanceRows rows{};
	di::Derived derived{};
	di::DeviceState leaderState{};
};
Verifying g_verifying;

bool LightingProperty(UInt32 vtable) {
	return vtable == kLightingPropertyVtables[0] || vtable == kLightingPropertyVtables[1];
}

UInt32 PropertyAt(UInt32 state, UInt32 slot) {
	return state != 0 ? Dword(state + kPropertySlots + 4 * slot) : 0;
}

// What the record step reads of a record: the facts, the group key and the
// prediction's key. Under SEH - a layout read wrongly must not end the game;
// no object with a destructor lives here. Two stages: the facts and the
// prediction's key decide whether the record may wait at all; only then the
// rest of the key is read.
struct RecordRead {
	di::Facts facts;
	di::Key key;
	di::PredictKey predict;
	UInt32 state = 0;  // the property state
};

void ReadFactsUnguarded(UInt32 geometry, UInt32 pass, RecordRead* out) {
	di::Facts& f = out->facts;
	f.geometry = true;
	f.skinned = Dword(geometry + kGeometrySkin) != 0;
	const UInt32 state = Dword(geometry + kGeometryProperties);
	out->state = state;
	const UInt32 alpha = PropertyAt(state, 0);
	const UInt32 material = PropertyAt(state, 2);
	const UInt32 shaderProperty = PropertyAt(state, 4);
	const UInt32 zbuffer = PropertyAt(state, 9);
	f.hasAlpha = alpha != 0;
	f.alphaFlags = alpha != 0 ? Word(alpha + kPropertyFlags) : 0;
	f.hasZ = zbuffer != 0;
	f.zFlags = zbuffer != 0 ? Word(zbuffer + kPropertyFlags) : 0;
	f.shaderAlpha = shaderProperty != 0 ? Float(shaderProperty + kShaderAlpha) : 1.0f;
	f.materialAlpha = material != 0 ? Float(material + kMaterialAlpha) : 1.0f;
	f.multiPass = !g_multiPass && shaderProperty != 0 && Word(shaderProperty + kShaderPassCount) > 1;
	out->predict = di::PredictKey{Dword(geometry + kGeometryShader), pass, shaderProperty != 0 ? Dword(shaderProperty + kShaderFlags) : 0};
}

void ReadKeyUnguarded(UInt32 record, UInt32 geometry, UInt32 pass, RecordRead* out) {
	di::Key& k = out->key;
	const UInt32 state = out->state;
	k.w[di::kWordPass] = pass;
	k.w[di::kWordData] = Dword(geometry + kGeometryData);
	k.w[di::kWordShader] = out->predict.shader;
	k.w[di::kWordShaderFlags] = out->predict.flags;
	// Shared by pointer (V1: the copies share them): fog, renderer specific,
	// texturing, wireframe. By value (each copy may have its own object with
	// equal values - measured 2026-11-12, the splits line): alpha (flags and
	// test reference), stencil (flags, reference, mask), vertex colour and
	// z-buffer (flags); 0 without the property, so that none is apart from
	// any value.
	k.w[di::kWordFogProperty] = PropertyAt(state, 1);
	k.w[di::kWordRendererProperty] = PropertyAt(state, 3);
	k.w[di::kWordTexturing] = PropertyAt(state, 6);
	k.w[di::kWordWireframe] = PropertyAt(state, 8);
	const UInt32 alpha = PropertyAt(state, 0);
	const UInt32 stencil = PropertyAt(state, 5);
	const UInt32 vertexColour = PropertyAt(state, 7);
	const UInt32 zbuffer = PropertyAt(state, 9);
	k.w[di::kWordAlphaProperty] = alpha != 0 ? 0x80000000u | Dword(alpha + kPropertyFlags) : 0;
	k.w[di::kWordVertexColour] = vertexColour != 0 ? 0x80000000u | Word(vertexColour + kPropertyFlags) : 0;
	k.w[di::kWordZBuffer] = zbuffer != 0 ? 0x80000000u | Word(zbuffer + kPropertyFlags) : 0;
	if (stencil != 0) {
		const std::uint32_t words[3] = {Word(stencil + kPropertyFlags), Dword(stencil + kStencilRef), Dword(stencil + kStencilMask)};
		k.w[di::kWordStencil] = di::HashWords(words, 3) | 1u;
	}
	const UInt32 shaderProperty = PropertyAt(state, 4);
	if (shaderProperty != 0) {
		const UInt32 vtable = Dword(shaderProperty);
		k.w[di::kWordShaderClass] = vtable;
		k.w[di::kWordLightScale] = LightingProperty(vtable) ? Dword(shaderProperty + kLightScale) : 0;
	}
	const UInt32 material = PropertyAt(state, 2);
	if (material != 0) {
		std::uint32_t values[kMaterialWords];
		for (UInt32 i = 0; i < kMaterialWords; ++i) {
			values[i] = Dword(material + kMaterialValues + 4 * i);
		}
		k.w[di::kWordMaterial] = di::HashWords(values, kMaterialWords);
	}
	const UInt32 lightCount = *reinterpret_cast<const volatile UInt8*>(record + kRecordLightCount);
	const UInt32 lights = Dword(record + kRecordLights);
	std::uint32_t recordLights[kMaxRecordLights] = {};
	const UInt32 counted = lights != 0 ? (lightCount < kMaxRecordLights ? lightCount : kMaxRecordLights) : 0;
	for (UInt32 i = 0; i < counted; ++i) {
		recordLights[i] = Dword(lights + 4 * i);
	}
	k.w[di::kWordLights] = di::LightsWord(recordLights, counted, g_lightsMode);
	const UInt32 effects = Dword(geometry + kGeometryEffects);
	if (effects != 0) {
		std::uint32_t words[5];
		for (UInt32 i = 0; i < 5; ++i) {
			words[i] = Dword(effects + kEffectWords + 4 * i);
		}
		k.w[di::kWordEffects] = di::HashWords(words, 5);
	}
	k.w[di::kWordRecord] = Dword(record + kRecordFlags);
}

bool ReadFacts(UInt32 geometry, UInt32 pass, RecordRead* out) {
	__try {
		ReadFactsUnguarded(geometry, pass, out);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadKey(UInt32 record, UInt32 geometry, UInt32 pass, RecordRead* out) {
	__try {
		ReadKeyUnguarded(record, geometry, pass, out);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// Whether the record is the walk's next item (the culler may have left some
// out before it); the cursor moves past it. *first: it is the list's head.
bool InWalk(UInt32 record, bool* first) {
	UInt32 node = g_list.cursor;
	for (UInt32 step = 0; step < kWalkLookahead && node != 0; ++step) {
		if (Dword(node + kNodeItem) == record) {
			*first = node == g_list.head;
			g_list.cursor = Dword(node + kNodeNext);
			return true;
		}
		node = Dword(node + kNodeNext);
	}
	return false;
}

// The whole of DrawRenderPass for a record again: its pass id, record and
// light constants published anew, then its draw (the record step lets it through).
void Replay(UInt32 record, UInt32 pass) {
	g_drawRecord(reinterpret_cast<void*>(g_list.accumulator), nullptr, record, pass);
}

// The geometry's world transform as the instance stream carries it.
di::InstanceData InstanceOf(UInt32 geometry) {
	float rotation[9];
	float translation[3];
	float origin[3];
	std::memcpy(rotation, reinterpret_cast<const void*>(geometry + kGeometryRotation), sizeof(rotation));
	std::memcpy(translation, reinterpret_cast<const void*>(geometry + kGeometryTranslation), sizeof(translation));
	std::memcpy(origin, reinterpret_cast<const void*>(kOriginGlobal), sizeof(origin));
	return di::PackInstance(rotation, translation, Float(geometry + kGeometryScale), origin);
}

// The second form of the shadow rows for the geometry (DrawInstancingLogic.h WorldRows).
void WorldRowsOf(UInt32 geometry, double out[4][4]) {
	float rotation[9];
	float translation[3];
	std::memcpy(rotation, reinterpret_cast<const void*>(geometry + kGeometryRotation), sizeof(rotation));
	std::memcpy(translation, reinterpret_cast<const void*>(geometry + kGeometryTranslation), sizeof(translation));
	di::WorldRows(rotation, translation, Float(geometry + kGeometryScale), out);
}

UInt32 RendererDevice() {
	const UInt32 renderer = Dword(0x00B3F928);  // g_renderer (xOBSE, OBVR GameAddresses.h)
	return renderer != 0 ? Dword(renderer + 0x280) : 0;
}

void ReplayMembers(const di::Pending::Group& group, UInt32 pass) {
	for (std::uint32_t r = g_pending.RecordAt(group.first).next; r != di::Pending::kNone; r = g_pending.RecordAt(r).next) {
		Replay(g_pending.RecordAt(r).record, pass);
	}
}

// Mode 2: the leader drawn once, instanced over the group; on any refusal the
// members are drawn one by one.
void EmitGroup(const di::Pending::Group& group, UInt32 pass, std::uint32_t rowsAt, std::uint32_t first, bool written) {
	const di::PredictKey predict{group.key.w[di::kWordShader], pass, group.key.w[di::kWordShaderFlags]};
	const di::Prediction* prediction = g_predictions.Find(predict);
	const di::Pending::Record& leader = g_pending.RecordAt(group.first);
	if (!written || prediction == nullptr) {
		++g_counts.fellBack[static_cast<int>(di::Fallback::kRoom)];
		Replay(leader.record, pass);
		ReplayMembers(group, pass);
		return;
	}
	g_emission = Emission{};
	g_emission.on = true;
	g_emission.geometry = leader.geometry;
	g_emission.passObject = prediction->passObject;
	g_emission.rows = g_rows[rowsAt].world;
	g_emission.first = first;
	g_emission.count = group.count;
	Replay(leader.record, pass);
	const Emission e = g_emission;
	g_emission = Emission{};
	if (e.used && !e.failed) {
		++g_counts.drawnInstanced;
		return;
	}
	++g_counts.fellBack[static_cast<int>(e.failed ? e.why : di::Fallback::kNotDrawn)];
	if (e.failed && (e.why == di::Fallback::kPassObject || e.why == di::Fallback::kProgram)) {
		g_predictions.Forget(predict);
	}
	ReplayMembers(group, pass);
}

// Mode 1: the leader's state read, then each member's compared with it.
void VerifyGroup(const di::Pending::Group& group, UInt32 pass) {
	const di::Pending::Record& leader = g_pending.RecordAt(group.first);
	g_verifying = Verifying{};
	g_verifying.on = true;
	g_verifying.leader = true;
	g_verifying.geometry = leader.geometry;
	g_verifying.record = leader.record;
	g_verifying.pass = pass;
	g_verifying.rows = InstanceOf(leader.geometry).world;
	g_verifying.leaderScale = Float(leader.geometry + kGeometryScale);
	Replay(leader.record, pass);
	for (std::uint32_t r = leader.next; r != di::Pending::kNone; r = g_pending.RecordAt(r).next) {
		const di::Pending::Record& member = g_pending.RecordAt(r);
		g_verifying.leader = false;
		g_verifying.done = false;
		g_verifying.geometry = member.geometry;
		g_verifying.record = member.record;
		g_verifying.rows = InstanceOf(member.geometry).world;
		Replay(member.record, pass);
	}
	g_verifying = Verifying{};
}

// The instance rows of every group of two or more, in group order, in one lock.
bool WriteGroupRows(std::uint32_t* base, std::uint32_t* firsts) {
	std::uint32_t n = 0;
	for (std::uint32_t g = 0; g < g_pending.GroupCount(); ++g) {
		const di::Pending::Group& group = g_pending.GroupAt(g);
		firsts[g] = n;
		if (group.count < 2) {
			continue;
		}
		for (std::uint32_t r = group.first; r != di::Pending::kNone; r = g_pending.RecordAt(r).next) {
			g_rows[n++] = InstanceOf(g_pending.RecordAt(r).geometry);
		}
	}
	const UInt32 device = RendererDevice();
	if (n == 0 || device == 0) {
		return false;
	}
	di::EnsureDeviceResetHook(device, g_log);
	return di::WriteInstances(device, g_rows, n, base);
}

void Flush(di::Flush why) {
	if (g_pending.Empty()) {
		return;
	}
	const unsigned __int64 t0 = __rdtsc();
	++g_counts.flushes[static_cast<int>(why)];
	g_splits.Add(g_pending);
	g_replaying = true;
	static std::uint32_t rowsAt[di::Pending::kMaxGroups];
	std::uint32_t base = 0;
	const bool written = g_mode == di::Mode::kInstanced && WriteGroupRows(&base, rowsAt);
	for (std::uint32_t g = 0; g < g_pending.GroupCount(); ++g) {
		const di::Pending::Group& group = g_pending.GroupAt(g);
		const UInt32 pass = group.key.w[di::kWordPass];
		++g_counts.groups;
		if (group.count < 2) {
			Replay(g_pending.RecordAt(group.first).record, pass);
			continue;
		}
		++g_counts.groupsMulti;
		g_counts.instances += group.count;
		if (g_mode == di::Mode::kVerify) {
			VerifyGroup(group, pass);
		} else {
			EmitGroup(group, pass, rowsAt[g], base + rowsAt[g], written);
		}
	}
	g_replaying = false;
	g_pending.Clear();
	g_counts.flushTicks += __rdtsc() - t0;
}

// The record step: DrawRenderPass 0x007A9820 right after it published its
// pass id and record (0x007A983B, before the record's light constants and
// draw). 1: the record waits - DrawRenderPass returns at once; 0: it goes on.
UInt32 __stdcall DecideRecord(UInt32 record) {
	if (g_list.depth != 1 || g_replaying || !g_onNow) {
		return 0;
	}
	const unsigned __int64 t0 = __rdtsc();
	++g_counts.records;
	const UInt32 geometry = Dword(record);
	const UInt32 pass = Dword(kPassIdGlobal) & 0xFFFFu;
	RecordRead r;
	r.facts.inList = true;
	r.facts.inWalk = InWalk(record, &r.facts.first);
	r.facts.readable = geometry != 0 && ReadFacts(geometry, pass, &r);
	r.facts.predicted = r.facts.readable && g_predictions.Predicts(r.predict);
	di::Why why = di::Decide(r.facts);
	if (why == di::Why::kWaits && !ReadKey(record, geometry, pass, &r)) {
		why = di::Why::kUnreadable;
		r.facts.readable = false;
	}
	++g_counts.why[static_cast<int>(why)];
	if (why == di::Why::kWaits) {
		di::LeaveOut(&r.key, g_ignored);
		const bool full = g_pending.Add(r.key, record, geometry) == di::Pending::Added::kFull;
		g_counts.siteTicks += __rdtsc() - t0;
		if (full) {
			Flush(di::Flush::kFull);
			g_pending.Add(r.key, record, geometry);
		}
		return 1;
	}
	g_drawing = Drawing{geometry, r.predict, r.facts.readable && r.facts.geometry && !r.facts.skinned, false};
	g_counts.siteTicks += __rdtsc() - t0;
	if (!g_pending.Empty() && !di::OrderFree(r.facts)) {
		// The waiting records first. Their replays publish their own pass id and
		// record; this record's are written back before DrawRenderPass goes on.
		Flush(di::Flush::kOrder);
		*reinterpret_cast<volatile UInt32*>(kPassIdGlobal) = pass;
		*reinterpret_cast<volatile UInt32*>(kPassRecordGlobal) = record;
	}
	return 0;
}

// The patch's stub: the displaced store, then the decision; EAX, ECX and EDX
// kept (the body does not rely on them here, but costs nothing to keep).
__declspec(naked) void RecordStep() {
	__asm {
		mov dword ptr ds:[0x00B42EB8], esi
		push eax
		push ecx
		push edx
		push esi
		call DecideRecord
		test eax, eax
		pop edx
		pop ecx
		pop eax
		jnz waits
		jmp dword ptr [g_recordStepContinue]
	waits:
		jmp dword ptr [g_recordStepReturn]
	}
}

// A record drawn at once: the pass object its shader drew with and whether
// the device held SLS2003's programs, noted for its key until the key has
// been seen often enough.
void Learn(UInt32 device, UInt32 shader, UInt32 geometry) {
	if (!g_drawing.learnable || g_drawing.learned || geometry != g_drawing.geometry) {
		return;
	}
	g_drawing.learned = true;
	if (shader == 0 || shader != g_drawing.predict.shader) {
		return;  // the cast gave the renderer's default shader
	}
	const di::Prediction* known = g_predictions.Find(g_drawing.predict);
	if (known != nullptr && (known->unstable || known->seen >= kLearnedAfter)) {
		return;
	}
	g_predictions.Note(g_drawing.predict, Dword(shader + kShaderPassObject), di::HoldsSls2003(device));
	++g_counts.learned;
}

// One line per geometry, the first kMismatchLines geometries of the run.
void LogMismatch(const char* what, double error, const char* more = "") {
	static UInt32 logged[kMismatchLines];
	if (g_mismatchLines >= kMismatchLines) {
		return;
	}
	for (UInt32 i = 0; i < g_mismatchLines; ++i) {
		if (logged[i] == g_verifying.geometry) {
			return;
		}
	}
	logged[g_mismatchLines++] = g_verifying.geometry;
	char name[48];
	ReadObjectName(g_verifying.geometry, name, sizeof(name));
	g_log("Route: draw instancing verify - \"%s\" (%08X) pass %u: %s apart (%.3e)%s", name, g_verifying.geometry, g_verifying.pass, what, error, more);
}

// For a ShadowProj apart: whether SetupTransformations computed it at all for
// this draw (the descriptor 0x00B4501C's enable byte, 0x007C9929 B28), the
// record's first light and its +0xF5 byte (B30), and whether the registers
// are the leader's own (left over from the draw before).
void ShadowDetail(const di::DeviceState& s, char* out, std::size_t size) {
	const UInt32 descriptor = Dword(0x00B4501C);
	const UInt32 lights = Dword(g_verifying.record + kRecordLights);
	const UInt32 first = lights != 0 ? Dword(lights) : 0;
	const bool leftOver = std::memcmp(s.vs[di::kRegisterShadow], g_verifying.leaderState.vs[di::kRegisterShadow], 4 * sizeof(s.vs[0])) == 0;
	std::snprintf(out, size, "; ShadowProj computed %u, %u lights, the first %08X with +0xF5 %u; the registers %s the leader's; c28 %.4g %.4g %.4g %.4g",
	              descriptor != 0 ? *reinterpret_cast<const volatile UInt8*>(descriptor + 8) : 0xFFu,
	              *reinterpret_cast<const volatile UInt8*>(g_verifying.record + kRecordLightCount), first,
	              first != 0 ? *reinterpret_cast<const volatile UInt8*>(first + 0xF5) : 0xFFu, leftOver ? "equal" : "not", s.vs[28][0], s.vs[28][1],
	              s.vs[28][2], s.vs[28][3]);
}

// Mode 1, at a replayed record's first device draw: the leader's state
// becomes the reference, a member's is compared with it.
void VerifyAt(UInt32 device, UInt32 geometry) {
	if (geometry != g_verifying.geometry || g_verifying.done) {
		return;
	}
	g_verifying.done = true;
	di::DeviceState s;
	if (!di::ReadDeviceState(device, &s)) {
		return;
	}
	if (g_verifying.leader) {
		g_verifying.haveLeader = di::Derive(s.vs, g_verifying.rows, &g_verifying.derived);
		g_verifying.leaderState = s;
		return;
	}
	if (!g_verifying.haveLeader) {
		return;
	}
	const di::Expected e = di::ExpectFor(g_verifying.derived, g_verifying.rows);
	double mvp = 0.0;
	for (int row = 0; row < 4; ++row) {
		mvp = mvp > di::RowError(s.vs[di::kRegisterViewProj + row], e.mvp[row]) ? mvp : di::RowError(s.vs[di::kRegisterViewProj + row], e.mvp[row]);
	}
	const double shadow = di::ShadowRowsError(s.vs, e.shadow);
	double worldRows[4][4];
	WorldRowsOf(g_verifying.geometry, worldRows);
	const double shadowWorld = di::ShadowRowsError(s.vs, worldRows);
	const double light = di::DirectionError(s.vs[di::kRegisterLight], e.light);
	const di::DeviceState& l = g_verifying.leaderState;
	const bool fog = std::memcmp(s.vs[23], l.vs[23], 2 * sizeof(s.vs[0])) == 0 && std::memcmp(s.vs[32], l.vs[32], 2 * sizeof(s.vs[0])) == 0;
	const bool pixel = std::memcmp(s.ps, l.ps, sizeof(s.ps)) == 0;
	const bool textures = std::memcmp(s.textures, l.textures, sizeof(s.textures)) == 0;
	const bool programs = s.vertexShader == l.vertexShader && s.pixelShader == l.pixelShader && s.declaration == l.declaration;
	g_verify.Note(di::Compared::kMvp, mvp);
	g_verify.Note(di::Compared::kLight, light);
	g_verify.Note(di::Compared::kShadow, shadow);
	g_verify.Note(di::Compared::kShadowWorld, shadowWorld);
	g_verify.NoteEqual(di::Compared::kShadowEither, shadow <= di::kRowTolerance || shadowWorld <= di::kRowTolerance);
	g_verify.NoteEqual(di::Compared::kFogAndData, fog);
	g_verify.NoteEqual(di::Compared::kPixel, pixel);
	g_verify.NoteEqual(di::Compared::kTextures, textures);
	g_verify.NoteEqual(di::Compared::kPrograms, programs);
	if (mvp > di::kRowTolerance) {
		char detail[200];
		std::snprintf(detail, sizeof(detail), "; scale %.4g against the leader's %.4g, translation %.6g %.6g %.6g against %.6g %.6g %.6g", Float(geometry + kGeometryScale),
		              g_verifying.leaderScale, s.vs[0][3], s.vs[1][3], s.vs[2][3], l.vs[0][3], l.vs[1][3], l.vs[2][3]);
		LogMismatch("ModelViewProj", mvp, detail);
	}
	if (light > di::kDirectionTolerance) {
		LogMismatch("the light direction", light);
	}
	if (shadow > di::kRowTolerance && shadowWorld > di::kRowTolerance) {
		char detail[256];
		ShadowDetail(s, detail, sizeof(detail));
		LogMismatch("ShadowProj in both forms", shadow, detail);
	}
	if (!pixel) {
		char detail[300];
		std::snprintf(detail, sizeof(detail), "; c1 %.4g %.4g %.4g %.4g against %.4g %.4g %.4g %.4g, c2 %.4g %.4g %.4g against %.4g %.4g %.4g, c7 %.3g %.3g against %.3g %.3g",
		              s.ps[0][0], s.ps[0][1], s.ps[0][2], s.ps[0][3], l.ps[0][0], l.ps[0][1], l.ps[0][2], l.ps[0][3], s.ps[1][0], s.ps[1][1], s.ps[1][2],
		              l.ps[1][0], l.ps[1][1], l.ps[1][2], s.ps[2][0], s.ps[2][1], l.ps[2][0], l.ps[2][1]);
		LogMismatch("the pixel constants", 0.0, detail);
	} else if (!fog || !textures || !programs) {
		LogMismatch(!fog ? "the fog or shadow data" : !textures ? "a texture" : "a program or the declaration", 0.0);
	}
}

void Fail(di::Fallback why) {
	g_emission.failed = true;
	g_emission.why = why;
}

// The summary lines, written by the render thread when asked for.
void WriteSummaryIfRequested() {
	if (g_summaryRequested == 0) {
		return;
	}
	InterlockedExchange(&g_summaryRequested, 0);
	const unsigned __int64 tsc = __rdtsc();
	LARGE_INTEGER qpc{};
	LARGE_INTEGER frequency{};
	QueryPerformanceCounter(&qpc);
	QueryPerformanceFrequency(&frequency);
	const double seconds = static_cast<double>(qpc.QuadPart - g_summaryQpc.QuadPart) / static_cast<double>(frequency.QuadPart);
	const double ticksPerMs = seconds > 0.0 && g_summaryTsc != 0 ? static_cast<double>(tsc - g_summaryTsc) / (seconds * 1000.0) : 0.0;
	g_summaryTsc = tsc;
	g_summaryQpc = qpc;
	char line[2000];
	di::Format(g_counts, g_mode, g_onNow, ticksPerMs, line, sizeof(line));
	g_log("%s", line);
	if (g_mode == di::Mode::kVerify) {
		di::FormatVerify(g_verify, line, sizeof(line));
		g_log("%s", line);
	}
	di::FormatSplits(g_splits, line, sizeof(line));
	g_log("%s", line);
	g_splits = di::Splits{};
	g_counts = di::Counts{};
	g_verify = di::VerifyCounts{};
	if (g_alternate == 1) {
		g_onNow = !g_onNow;
	}
}

// BSShaderAccumulator::RenderPassList's replacement: the list's begin, the
// original, and the waiting records drawn at its end.
UInt32 __fastcall RenderPassList(void* self, void* /*edx*/, UInt32 pass, UInt32 mode) {
	if (g_list.depth == 0) {
		WriteSummaryIfRequested();
		const UInt32 clock = Dword(kClockMs);
		if (clock != g_lastClock) {
			g_lastClock = clock;
			++g_counts.frames;
			if (g_alternate == 2) {
				g_onNow = !g_onNow;
			}
			++g_counts.parityFrames[g_onNow ? 1 : 0];
		}
	} else {
		++g_counts.nestedLists;
	}
	const unsigned __int64 t0 = g_list.depth == 0 ? __rdtsc() : 0;
	const List outer = g_list;
	const UInt32 accumulator = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self));
	g_list.depth = outer.depth + 1;
	g_list.accumulator = accumulator;
	const UInt32 list = accumulator + kLists + kListStride * (pass & 0xFFFFu);
	g_list.head = Dword(list + kListHead);
	g_list.cursor = g_list.head;
	const bool stamped = gpu_passes::g_on && g_list.depth == 1;
	if (stamped) {
		gpu_passes::ListBegin(accumulator, pass, Dword(list + kListCount), g_list.head != 0 ? Dword(Dword(g_list.head + kNodeItem)) : 0);
	}
	const UInt32 eax = g_renderPassList(self, nullptr, pass, mode);
	if (g_list.depth == 1) {
		Flush(di::Flush::kListEnd);
		g_counts.listTicks[g_onNow ? 1 : 0] += __rdtsc() - t0;
	}
	if (stamped) {
		gpu_passes::ListEnd();
	}
	g_list = outer;
	return eax;
}

// Writes a jump to RecordStep and a nop over the record step's 6 bytes, after
// checking they are the game's own.
bool PatchRecordStep() {
	if (!Verify(kRecordStep, kRecordStepBytes, sizeof(kRecordStepBytes))) {
		char have[32];
		DescribeBytes(kRecordStep, have, sizeof(have));
		g_log("Route: draw instancing - DrawRenderPass's record step at %08X reads %s, not the game's own bytes; not installed", kRecordStep, have);
		return false;
	}
	UInt8 patch[sizeof(kRecordStepBytes)];
	patch[0] = 0xE9;  // jmp rel32
	const UInt32 relative = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&RecordStep)) - (kRecordStep + 5);
	std::memcpy(&patch[1], &relative, sizeof(relative));
	patch[5] = 0x90;
	return SafeWrite(kRecordStep, patch, sizeof(patch));
}

}  // namespace

namespace draw_instancing {

bool RouteIndexedDraw(std::uint32_t device, std::uint32_t shader, std::uint32_t geometry, const IndexedCall& call) {
	if (!g_replaying) {
		Learn(device, shader, geometry);
		return false;
	}
	if (g_verifying.on) {
		VerifyAt(device, geometry);
		return false;
	}
	if (!g_emission.on || g_emission.failed || geometry != g_emission.geometry) {
		return false;
	}
	// Every device draw: a second pass of the record (slot 19) has another pass object.
	if (shader == 0 || Dword(shader + kShaderPassObject) != g_emission.passObject) {
		Fail(Fallback::kPassObject);
		return false;
	}
	if (!g_emission.checked) {
		g_emission.checked = true;
		if (!HoldsSls2003(device)) {
			Fail(Fallback::kProgram);
			return false;
		}
	}
	float registers[kVertexRegisters][4];
	Derived derived;
	Fallback why = Fallback::kDevice;
	if (!ReadVertexRegisters(device, registers) || !Derive(registers, g_emission.rows, &derived)) {
		Fail(why);
		return false;
	}
	// Which form the leader's shadow rows have: its own world rows (then every
	// copy computes its own), or another affine map of the packed world (then
	// the derived rows serve every copy).
	double worldRows[4][4];
	WorldRowsOf(geometry, worldRows);
	const float shadowMode = ShadowRowsError(registers, worldRows) <= 1e-6 ? 1.0f : 0.0f;
	const unsigned __int64 t0 = __rdtsc();
	const bool drawn = DrawInstanced(device, derived, registers, shadowMode, g_emission.first, g_emission.count, call, &why);
	g_counts.instancedTicks += __rdtsc() - t0;
	++g_counts.instancedCalls;
	if (!drawn) {
		Fail(why);
		return false;
	}
	g_emission.used = true;
	return true;
}

void RoutePlainDraw(std::uint32_t geometry) {
	if (g_replaying && g_emission.on && !g_emission.failed && geometry == g_emission.geometry) {
		Fail(Fallback::kPlainDraw);
	}
}

}  // namespace draw_instancing

void LogDrawInstancingSummary() {
	if (draw_instancing::g_on) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

void InstallDrawInstancing(const char* iniPath, const RouteSettings& route, LogFn log) {
	static bool s_done = false;
	if (s_done) {
		return;
	}
	s_done = true;
	g_log = log;
	bool known = false;
	g_mode = di::ModeFromIni(static_cast<int>(GetPrivateProfileIntA("Route", "DrawInstancing", 0, iniPath)), &known);
	g_alternate = GetPrivateProfileIntA("Route", "DrawInstancingAlternate", 0, iniPath);
	g_lightsMode = GetPrivateProfileIntA("Route", "DrawInstancingLights", 2, iniPath);
	g_multiPass = GetPrivateProfileIntA("Route", "DrawInstancingMultiPass", 0, iniPath) != 0;
	g_lightsMode = g_lightsMode > 2 ? 2 : g_lightsMode;
	char ignored[200] = {};
	GetPrivateProfileStringA("Route", "DrawInstancingIgnore", "", ignored, sizeof(ignored), iniPath);
	int unknownWords = 0;
	g_ignored = di::IgnoredWords(ignored, &unknownWords);
	if (unknownWords != 0) {
		log("Route: draw instancing - %d unknown word(s) in [Route] DrawInstancingIgnore=%s; left in the key", unknownWords, ignored);
	}
	if (!known) {
		log("Route: draw instancing - [Route] DrawInstancing is not 0, 1 or 2; off");
	}
	// The pass list hook also serves [Probe] GpuPasses (GpuPassTimer.h), with the instancing off.
	const bool instancing = g_mode != di::Mode::kOff;
	if (!instancing && !gpu_passes::g_on) {
		return;
	}
	if (instancing && !route.stereoDraw) {
		log("Route: draw instancing needs [Route] StereoDraw=1 (the instanced draw takes the whole draw's device call); not installed");
		return;
	}
	auto* trampoline = static_cast<UInt8*>(AllocExecutable(32));
	if (trampoline == nullptr ||
	    BuildEntryTrampoline(trampoline, 32, static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(trampoline)), kDrawRenderPass, kDrawRenderPassBytes,
	                         sizeof(kDrawRenderPassBytes)) == 0) {
		log("Route: draw instancing - no trampoline for DrawRenderPass; not installed");
		return;
	}
	g_drawRecord = reinterpret_cast<DrawRenderPassFn>(trampoline);
	if (!InstallEntryDetour("BSShaderAccumulator::RenderPassList 0x007AC9A0 (draw instancing: the list's begin and end)", kRenderPassList, kRenderPassListBytes,
	                        sizeof(kRenderPassListBytes), reinterpret_cast<const void*>(&RenderPassList), reinterpret_cast<void**>(&g_renderPassList), log,
	                        DetourRole::Route)) {
		return;
	}
	if (!instancing) {
		return;  // the list hook only, for the GPU pass timer
	}
	if (!PatchRecordStep()) {
		return;  // the list hook runs on; nothing reaches the record step, nothing waits
	}
	draw_instancing::g_on = true;
	log("Route: draw instancing (%s%s) - on: DrawRenderPass's record step at %08X patched; records of SLS2003 passes wait for their list's end; "
	    "lights in the key %u (2 all, 1 the first, 0 none), words left out of the key %08X",
	    di::ModeName(g_mode), g_alternate == 1 ? ", alternating by summary window" : g_alternate == 2 ? ", alternating by frame" : "", kRecordStep, g_lightsMode,
	    g_ignored);
}

}  // namespace obng::plugin
