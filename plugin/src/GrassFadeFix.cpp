// The grass fade fix ([Fix] GrassFade): GrassFadePatchLogic.h has the fault,
// the patch and the decisions; this file is the device side.
//
// Two entries of the device's method table are replaced (the table is the
// device class's; slot numbers by the method order of IDirect3DDevice9 in
// d3d9.h: CreateVertexShader 91, SetVertexShader 92 - FoliageAA's
// DeviceHook.h lists the same and checks them against a real device):
//
//   SetVertexShader: a shader seen for the first time is asked for its byte
//     code (IDirect3DVertexShader9::GetFunction); when PatchGrassFade makes
//     a twin of it, the twin is created on the device and remembered. A
//     shader with a twin is bound as its twin. The twin needs nothing
//     uploaded: it takes the projection's scales from the rows of the
//     ModelViewProj the game uploads for it.
//   CreateVertexShader: a new shader may get the address of a released one;
//     what was remembered for that address is forgotten (its twin released).
//
// The twin is created through the method table, so other modules hooked
// there (FoliageAA) see it. FoliageAA knows a grass vertex shader by its
// constant table, which the twin keeps.
//
// The device comes into being after the plugin is loaded: a thread waits
// for the renderer ([0x00B3F928]; re/functions/0x007C78D0.md, OBVR's
// GameAddresses.h) and its device (+0x280; re/functions/0x00762640.md,
// xOBSE) and then writes the two entries.

#include "GrassFadeFix.h"

#include <windows.h>

#include <d3d9.h>

#include <cstring>
#include <unordered_map>
#include <vector>

#include "GrassFadePatchLogic.h"
#include "Memory.h"
#include "Types.h"

namespace obng::plugin {

namespace {

namespace gf = grass_fade;

constexpr UInt32 kRendererGlobal = 0x00B3F928;
constexpr UInt32 kRendererDeviceOffset = 0x280;
constexpr int kSlotCreateVertexShader = 91;
constexpr int kSlotSetVertexShader = 92;
constexpr DWORD kWaitStepMs = 250;

using CreateVertexShaderFn = HRESULT(__stdcall*)(IDirect3DDevice9* device, const DWORD* function, IDirect3DVertexShader9** shader);
using SetVertexShaderFn = HRESULT(__stdcall*)(IDirect3DDevice9* device, IDirect3DVertexShader9* shader);

LogFn g_log = nullptr;
FixSettings g_settings;
CreateVertexShaderFn g_originalCreate = nullptr;
SetVertexShaderFn g_originalSet = nullptr;

// What is known of each vertex shader the game bound: its twin, or null for
// a shader that gets none. Shaders are created on loader threads too.
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<IDirect3DVertexShader9*, IDirect3DVertexShader9*> g_twins;
UInt32 g_twinsMade = 0;
UInt32 g_refusalsLogged = 0;
// The binding thread's table in front of the record (GrassFadePatchLogic.h
// BindCache), and the generation a forgotten shader moves on.
gf::BindCache g_bindCache;
volatile LONG g_generation = 1;

// The render thread's: whether the last bind was a twin's.
bool g_twinBound = false;

template <class T>
T ReadAt(std::uintptr_t address) {
	return *reinterpret_cast<volatile const T*>(address);
}

// The twin of a shader seen for the first time, or null.
IDirect3DVertexShader9* MakeTwin(IDirect3DDevice9* device, IDirect3DVertexShader9* shader) {
	UINT size = 0;
	if (FAILED(shader->GetFunction(nullptr, &size)) || size < 2 * sizeof(DWORD)) {
		return nullptr;
	}
	std::vector<std::uint32_t> code(size / sizeof(std::uint32_t));
	UINT got = static_cast<UINT>(code.size() * sizeof(std::uint32_t));
	if (FAILED(shader->GetFunction(code.data(), &got))) {
		return nullptr;
	}
	std::vector<std::uint32_t> twinCode;
	const gf::Patch patch =
	    gf::PatchGrassFade(code.data(), code.size(), g_settings.grassFadeIniReach ? gf::Reach::kIni : gf::Reach::kGame, &twinCode);
	if (patch.verdict != gf::Verdict::kPatched) {
		// A shader with the grass's constant table that could not be patched is worth a line; every other shader is not.
		if (patch.verdict != gf::Verdict::kNotGrass && patch.verdict != gf::Verdict::kNotVertexShader2 && g_refusalsLogged < 8) {
			++g_refusalsLogged;
			g_log("Fix: grass fade - a grass vertex shader (%p) left as it is: %s", static_cast<void*>(shader), gf::VerdictName(patch.verdict));
		}
		return nullptr;
	}
	IDirect3DVertexShader9* twin = nullptr;
	if (FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(twinCode.data()), &twin)) || twin == nullptr) {
		if (g_refusalsLogged < 8) {
			++g_refusalsLogged;
			g_log("Fix: grass fade - the device refused the twin of the grass vertex shader %p; the game's shader stays", static_cast<void*>(shader));
		}
		return nullptr;
	}
	++g_twinsMade;
	g_log("Fix: grass fade - twin %u made: vertex shader %p drawn as %p (the clip position r%u, ModelViewProj's rows from c%u, the temps r%u and r%u)",
	      g_twinsMade, static_cast<void*>(shader), static_cast<void*>(twin), patch.clipRegister, patch.rowRegister, patch.tempRegister,
	      patch.tempRegister + 1);
	return twin;
}

IDirect3DVertexShader9* TwinFromRecord(IDirect3DDevice9* device, IDirect3DVertexShader9* shader) {
	AcquireSRWLockShared(&g_lock);
	const auto found = g_twins.find(shader);
	const bool known = found != g_twins.end();
	IDirect3DVertexShader9* twin = known ? found->second : nullptr;
	ReleaseSRWLockShared(&g_lock);
	if (known) {
		return twin;
	}
	// Outside the lock: creating the twin comes back through HookCreateVertexShader.
	twin = MakeTwin(device, shader);
	AcquireSRWLockExclusive(&g_lock);
	const auto [at, inserted] = g_twins.emplace(shader, twin);
	IDirect3DVertexShader9* kept = at->second;
	ReleaseSRWLockExclusive(&g_lock);
	if (!inserted && twin != nullptr && twin != kept) {
		twin->Release();  // another thread was first
	}
	return kept;
}

IDirect3DVertexShader9* TwinOf(IDirect3DDevice9* device, IDirect3DVertexShader9* shader) {
	const std::uint32_t generation = static_cast<std::uint32_t>(g_generation);
	std::uintptr_t cached = 0;
	if (g_bindCache.Find(reinterpret_cast<std::uintptr_t>(shader), generation, &cached)) {
		return reinterpret_cast<IDirect3DVertexShader9*>(cached);
	}
	IDirect3DVertexShader9* const found = TwinFromRecord(device, shader);
	// Remembered only when nothing was forgotten meanwhile (making a twin creates a shader).
	if (static_cast<std::uint32_t>(g_generation) == generation) {
		g_bindCache.Put(reinterpret_cast<std::uintptr_t>(shader), reinterpret_cast<std::uintptr_t>(found), generation);
	}
	return found;
}

HRESULT __stdcall HookSetVertexShader(IDirect3DDevice9* device, IDirect3DVertexShader9* shader) {
	IDirect3DVertexShader9* twin = shader != nullptr ? TwinOf(device, shader) : nullptr;
	if (twin != nullptr) {
		const HRESULT hr = g_originalSet(device, twin);
		if (SUCCEEDED(hr)) {
			g_twinBound = true;
			return hr;
		}
	}
	g_twinBound = false;
	return g_originalSet(device, shader);
}

HRESULT __stdcall HookCreateVertexShader(IDirect3DDevice9* device, const DWORD* function, IDirect3DVertexShader9** shader) {
	const HRESULT hr = g_originalCreate(device, function, shader);
	if (SUCCEEDED(hr) && shader != nullptr && *shader != nullptr) {
		IDirect3DVertexShader9* stale = nullptr;
		AcquireSRWLockExclusive(&g_lock);
		const auto found = g_twins.find(*shader);
		if (found != g_twins.end()) {
			stale = found->second;
			g_twins.erase(found);
			if (InterlockedIncrement(&g_generation) == 0) {
				InterlockedIncrement(&g_generation);  // 0 marks an empty entry of the bind cache
			}
		}
		ReleaseSRWLockExclusive(&g_lock);
		if (stale != nullptr) {
			stale->Release();
		}
	}
	return hr;
}

DWORD WINAPI WaitForDevice(LPVOID) {
	for (;;) {
		const UInt32 renderer = ReadAt<UInt32>(kRendererGlobal);
		const UInt32 device = renderer != 0 ? ReadAt<UInt32>(renderer + kRendererDeviceOffset) : 0u;
		if (device != 0) {
			void** table = *reinterpret_cast<void***>(static_cast<std::uintptr_t>(device));
			g_originalCreate = reinterpret_cast<CreateVertexShaderFn>(table[kSlotCreateVertexShader]);
			g_originalSet = reinterpret_cast<SetVertexShaderFn>(table[kSlotSetVertexShader]);
			void* const create = reinterpret_cast<void*>(&HookCreateVertexShader);
			void* const set = reinterpret_cast<void*>(&HookSetVertexShader);
			// SetVertexShader last: its hook is the one that acts, and it needs both originals in place.
			if (SafeWrite(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&table[kSlotCreateVertexShader])), &create, sizeof(create)) &&
			    SafeWrite(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&table[kSlotSetVertexShader])), &set, sizeof(set))) {
				g_log("Fix: grass fade - the device %08X is there, CreateVertexShader and SetVertexShader hooked in its method table at %p", device,
				      static_cast<void*>(table));
			} else {
				g_log("Fix: grass fade - the device's method table at %p could not be written; the game's grass shaders stay", static_cast<void*>(table));
			}
			return 0;
		}
		Sleep(kWaitStepMs);
	}
}

}  // namespace

bool InstallGrassFadeFix(const FixSettings& settings, LogFn log) {
	g_log = log;
	g_settings = settings;
	HANDLE thread = CreateThread(nullptr, 0, &WaitForDevice, nullptr, 0, nullptr);
	if (thread == nullptr) {
		return false;
	}
	CloseHandle(thread);
	return true;
}

int GrassFadeFixBoundTwin() {
	return g_twinBound ? (g_settings.grassFadeIniReach ? 1 : 2) : 0;
}

}  // namespace obng::plugin
