#include "GpuPassTimer.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdio>
#include <cstring>

#include "GpuPassTimerLogic.h"
#include "Memory.h"

namespace obng::plugin {

namespace {

namespace gp = gpu_passes;

// The actor shadow maps, RenderScene's first step (re/functions/0x0040C830.md
// step 4; __thiscall, no stack argument, RET at 0x00407AD7 - re/functions/
// 0x004073D0.md) and the image space step (cdecl(renderer, target, argument),
// UnroutedDrawProbe.cpp). Entry bytes: objdump 2026-11-12 / UnroutedDrawProbe.cpp.
constexpr UInt32 kActorShadows = 0x004073D0;
constexpr UInt8 kActorShadowsBytes[] = {0x83, 0xEC, 0x4C, 0x83, 0x3D, 0x48, 0x2F, 0xB4, 0x00, 0x03};  // sub esp,4Ch; cmp dword [0x00B42F48],3
constexpr UInt32 kImageSpace = 0x007B48E0;
constexpr UInt8 kImageSpaceBytes[] = {0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x4C, 0x24, 0x08};  // mov eax,[esp+0Ch]; mov ecx,[esp+8]
constexpr UInt32 kRendererGlobal = 0x00B3F928;
constexpr UInt32 kRendererDevice = 0x280;
constexpr UInt32 kGeometryShader = 0xBC;
constexpr UInt32 kDeviceResetSlot = 16;

using ActorShadowsFn = void(__fastcall*)(void* self, void* edx);
using ImageSpaceFn = void(__cdecl*)(UInt32 renderer, UInt32 target, UInt32 argument);
using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* parameters);

ActorShadowsFn g_actorShadows = nullptr;
ImageSpaceFn g_imageSpace = nullptr;
ResetFn g_originalReset = nullptr;
LogFn g_log = nullptr;
UInt32 g_every = 4;
UInt32 g_frameCount = 0;

// One sampled frame: its stamps (a query each at a span's begin and end), the
// disjoint and frequency queries.
constexpr UInt32 kFrames = 6;
constexpr UInt32 kSpans = 384;
struct Frame {
	bool open = false;     // stamps being issued
	bool pending = false;  // closed, waiting for the read-back
	UInt32 age = 0;        // frames since it closed
	IDirect3DQuery9* disjoint = nullptr;
	IDirect3DQuery9* frequency = nullptr;
	IDirect3DQuery9* begin[kSpans] = {};
	IDirect3DQuery9* end[kSpans] = {};
	gp::Span spans[kSpans];
	bool ended[kSpans] = {};
	UInt32 count = 0;
	UInt32 openList = kSpans;  // the list span awaiting its end
	gp::FinishTracker finishes;
};
Frame g_frames[kFrames];
UInt32 g_current = kFrames;  // the open frame, or kFrames
IDirect3DDevice9* g_device = nullptr;
gp::Totals g_totals;
volatile LONG g_summaryRequested = 0;

IDirect3DDevice9* Device() {
	const UInt32 renderer = *reinterpret_cast<const volatile UInt32*>(kRendererGlobal);
	return renderer != 0 ? *reinterpret_cast<IDirect3DDevice9* const volatile*>(renderer + kRendererDevice) : nullptr;
}

void ReleaseQueries() {
	for (Frame& f : g_frames) {
		auto release = [](IDirect3DQuery9*& q) {
			if (q != nullptr) {
				q->Release();
				q = nullptr;
			}
		};
		release(f.disjoint);
		release(f.frequency);
		for (UInt32 i = 0; i < kSpans; ++i) {
			release(f.begin[i]);
			release(f.end[i]);
		}
		f.open = false;
		f.pending = false;
		f.count = 0;
	}
	g_current = kFrames;
}

HRESULT __stdcall HookedReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* parameters) {
	if (self == g_device) {
		ReleaseQueries();
	}
	return g_originalReset(self, parameters);
}

// Once: a second hook on the slot would call itself through another's re-hook.
void EnsureResetHook(IDirect3DDevice9* device) {
	static bool tried = false;
	if (tried) {
		return;
	}
	tried = true;
	void** vtable = *reinterpret_cast<void***>(device);
	void* hook = reinterpret_cast<void*>(&HookedReset);
	g_originalReset = reinterpret_cast<ResetFn>(vtable[kDeviceResetSlot]);
	if (!SafeWrite(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&vtable[kDeviceResetSlot])), &hook, sizeof(hook))) {
		g_originalReset = nullptr;
		g_log("Probe: GPU passes - the device's vtable could not be written; the probe stays off");
		gp::g_on = false;
	}
}

bool Ensure(IDirect3DDevice9* device, IDirect3DQuery9** q, D3DQUERYTYPE type) {
	return *q != nullptr || SUCCEEDED(device->CreateQuery(type, q));
}

// A stamp: the span's begin or end query issued.
bool Stamp(IDirect3DQuery9** q) {
	if (!Ensure(g_device, q, D3DQUERYTYPE_TIMESTAMP)) {
		return false;
	}
	(*q)->Issue(D3DISSUE_END);
	return true;
}

// The span that begins now in the open frame (its slot taken at once: spans
// nest - the actor shadows hold the shadow maps' lists), or kSpans.
UInt32 Begin(gp::Kind kind) {
	if (g_current == kFrames || g_device == nullptr) {
		return kSpans;
	}
	Frame& f = g_frames[g_current];
	if (f.count == kSpans || !Ensure(g_device, &f.end[f.count], D3DQUERYTYPE_TIMESTAMP) || !Stamp(&f.begin[f.count])) {
		return kSpans;
	}
	const UInt32 i = f.count++;
	f.spans[i] = gp::Span{};
	f.spans[i].kind = kind;
	f.ended[i] = false;
	return i;
}

// The span's end, when its frame is still the open one.
void End(UInt32 frame, UInt32 i) {
	if (i == kSpans || frame != g_current || frame == kFrames) {
		return;
	}
	Frame& f = g_frames[frame];
	f.ended[i] = Stamp(&f.end[i]);
}

// The oldest closed frames read back, without waiting; a frame not back
// after kFrames - 1 newer ones is given up.
void ReadBack() {
	for (Frame& f : g_frames) {
		if (!f.pending) {
			continue;
		}
		++f.age;
		BOOL disjoint = FALSE;
		UINT64 frequency = 0;
		bool ready = f.disjoint->GetData(&disjoint, sizeof(disjoint), 0) == S_OK && f.frequency->GetData(&frequency, sizeof(frequency), 0) == S_OK;
		for (UInt32 i = 0; ready && i < f.count; ++i) {
			if (!f.ended[i]) {
				f.spans[i].begin = f.spans[i].end = 0;  // closed with the frame before its end: counts 0
				continue;
			}
			ready = f.begin[i]->GetData(&f.spans[i].begin, sizeof(UINT64), 0) == S_OK && f.end[i]->GetData(&f.spans[i].end, sizeof(UINT64), 0) == S_OK;
		}
		if (ready) {
			f.pending = false;
			if (disjoint) {
				++g_totals.disjoint;
			} else {
				g_totals.AddFrame(f.spans, f.count, frequency);
			}
		} else if (f.age >= kFrames - 1) {
			f.pending = false;
			++g_totals.lost;
		}
	}
}

void CloseFrame() {
	if (g_current == kFrames) {
		return;
	}
	Frame& f = g_frames[g_current];
	f.disjoint->Issue(D3DISSUE_END);
	f.frequency->Issue(D3DISSUE_END);
	f.open = false;
	f.pending = true;
	f.age = 0;
	g_current = kFrames;
}

void WriteSummaryIfRequested();

// The frame's start: the actor shadows step. The frame before is closed, the
// closed ones read, and every n-th frame a free record opened.
void FrameStart() {
	WriteSummaryIfRequested();
	CloseFrame();
	IDirect3DDevice9* device = Device();
	if (device == nullptr) {
		return;
	}
	if (device != g_device) {
		ReleaseQueries();
		g_device = device;
		EnsureResetHook(device);
	}
	ReadBack();
	if (!gp::g_on || (g_frameCount++ % g_every) != 0) {
		return;
	}
	for (UInt32 i = 0; i < kFrames; ++i) {
		Frame& f = g_frames[i];
		if (f.pending || f.open) {
			continue;
		}
		if (!Ensure(device, &f.disjoint, D3DQUERYTYPE_TIMESTAMPDISJOINT) || !Ensure(device, &f.frequency, D3DQUERYTYPE_TIMESTAMPFREQ)) {
			return;
		}
		f.disjoint->Issue(D3DISSUE_BEGIN);
		f.open = true;
		f.count = 0;
		f.openList = kSpans;
		f.finishes = gp::FinishTracker{};
		g_current = i;
		return;
	}
}

void __fastcall ActorShadows(void* self, void* /*edx*/) {
	FrameStart();
	const UInt32 frame = g_current;
	const UInt32 i = Begin(gp::Kind::kActorShadows);
	g_actorShadows(self, nullptr);
	End(frame, i);
}

void __cdecl ImageSpace(UInt32 renderer, UInt32 target, UInt32 argument) {
	const UInt32 frame = g_current;
	const UInt32 i = Begin(gp::Kind::kImageSpace);
	g_imageSpace(renderer, target, argument);
	End(frame, i);
}

// The render target's size, width << 16 | height.
UInt32 TargetSize() {
	IDirect3DSurface9* surface = nullptr;
	if (FAILED(g_device->GetRenderTarget(0, &surface)) || surface == nullptr) {
		return 0;
	}
	D3DSURFACE_DESC desc{};
	surface->GetDesc(&desc);
	surface->Release();
	return (desc.Width << 16) | (desc.Height & 0xFFFFu);
}

UInt32 ShaderOf(UInt32 geometry) {
	__try {
		const UInt32 shader = geometry != 0 ? *reinterpret_cast<const UInt32*>(geometry + kGeometryShader) : 0;
		return shader != 0 ? *reinterpret_cast<const UInt32*>(shader) : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

// The class name of a vtable from its RTTI (the complete object locator at
// vtable - 4, its type descriptor at +0x0C, the decorated name at +0x08 of
// that, ".?AV<name>@@"); "?" when unreadable.
void ClassName(UInt32 vtable, char* out, std::size_t size) {
	std::snprintf(out, size, "?");
	if (vtable == 0) {
		return;
	}
	__try {
		const UInt32 locator = *reinterpret_cast<const UInt32*>(vtable - 4);
		const UInt32 type = *reinterpret_cast<const UInt32*>(locator + 0x0C);
		const char* name = reinterpret_cast<const char*>(type + 0x08);
		std::size_t n = 0;
		const std::size_t skip = (name[0] == '.' && name[1] == '?' && name[2] == 'A' && name[3] == 'V') ? 4 : 0;
		while (n + 1 < size && name[skip + n] != '\0' && name[skip + n] != '@') {
			out[n] = name[skip + n];
			++n;
		}
		out[n] = '\0';
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		std::snprintf(out, size, "?");
	}
}

void WriteSummaryIfRequested() {
	if (g_summaryRequested == 0) {
		return;
	}
	InterlockedExchange(&g_summaryRequested, 0);
	char line[2400];
	gp::FormatFrame(g_totals, line, sizeof(line));
	g_log("%s", line);
	constexpr UInt32 kTop = 20;
	UInt32 order[kTop];
	const UInt32 n = gp::LargestLists(g_totals, order, kTop);
	const double frames = g_totals.frames != 0 ? static_cast<double>(g_totals.frames) : 1.0;
	int used = std::snprintf(line, sizeof(line), "Probe: GPU passes - the largest pass lists, ms a frame (finish/pass id: class, records a frame):");
	for (UInt32 i = 0; i < n && used > 0 && static_cast<std::size_t>(used) < sizeof(line); ++i) {
		const gp::Totals::List& l = g_totals.list[order[i]];
		char name[48];
		ClassName(l.shader, name, sizeof(name));
		used += std::snprintf(line + used, sizeof(line) - static_cast<std::size_t>(used), " #%u/%u %.3f (%s, %.1f);", l.key >> 16, l.key & 0xFFFFu, l.ms / frames,
		                      name, static_cast<double>(l.records) / frames);
	}
	g_log("%s", line);
	if (g_totals.listsDropped != 0) {
		g_log("Probe: GPU passes - %llu list spans not kept (the table full)", static_cast<unsigned long long>(g_totals.listsDropped));
	}
	g_totals = gp::Totals{};
}

}  // namespace

namespace gpu_passes {

void ListBegin(std::uint32_t accumulator, std::uint32_t pass, std::uint32_t records, std::uint32_t firstGeometry) {
	if (g_current == kFrames || records == 0) {
		return;
	}
	Frame& f = g_frames[g_current];
	bool fresh = false;
	const UInt32 finish = f.finishes.Next(accumulator, pass & 0xFFFFu, &fresh);
	const UInt32 i = Begin(Kind::kList);
	if (i == kSpans) {
		return;
	}
	Span& s = f.spans[i];
	s.finish = finish;
	s.pass = pass & 0xFFFFu;
	s.records = records;
	s.shader = ShaderOf(firstGeometry);
	s.target = fresh ? TargetSize() : 0;
	f.openList = i;
}

void ListEnd() {
	if (g_current == kFrames) {
		return;
	}
	Frame& f = g_frames[g_current];
	End(g_current, f.openList);
	f.openList = kSpans;
}

}  // namespace gpu_passes

void LogGpuPassTimerSummary() {
	if (gp::g_on) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

void InstallGpuPassTimer(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || GetPrivateProfileIntA("Probe", "GpuPasses", 0, iniPath) == 0) {
		return;
	}
	s_done = true;
	g_log = log;
	const UINT every = GetPrivateProfileIntA("Probe", "GpuPassesEvery", 4, iniPath);
	g_every = every < 1 ? 1 : every > 64 ? 64 : every;
	if (GetPrivateProfileIntA("Probe", "UnroutedDraws", 0, iniPath) != 0) {
		log("Probe: GPU passes - not beside [Probe] UnroutedDraws (both take the image space's entry); off");
		return;
	}
	const bool shadows = InstallEntryDetour("the actor shadows 0x004073D0 (GPU passes: the frame's start)", kActorShadows, kActorShadowsBytes,
	                                        sizeof(kActorShadowsBytes), reinterpret_cast<const void*>(&ActorShadows), reinterpret_cast<void**>(&g_actorShadows), log);
	const bool image = shadows && InstallEntryDetour("the image space step 0x007B48E0 (GPU passes)", kImageSpace, kImageSpaceBytes, sizeof(kImageSpaceBytes),
	                                                 reinterpret_cast<const void*>(&ImageSpace), reinterpret_cast<void**>(&g_imageSpace), log);
	gp::g_on = shadows && image;
	log("Probe: GPU passes %s - one frame of %u stamped (actor shadows, every pass list, image space)", gp::g_on ? "on" : "NOT on", g_every);
}

}  // namespace obng::plugin
