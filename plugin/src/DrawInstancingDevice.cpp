#include "DrawInstancingDevice.h"

#include <windows.h>

#include <d3d9.h>

#include <cstring>

#include "Memory.h"
#include "shaders/SLS2003InstancedBytecode.h"

namespace obng::plugin::draw_instancing {

namespace {

IDirect3DDevice9* AsDevice(std::uint32_t device) {
	return reinterpret_cast<IDirect3DDevice9*>(static_cast<std::uintptr_t>(device));
}

std::uint32_t Identity(const void* object) {
	return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
}

template <class T>
void ReleaseIf(T* object) {
	if (object != nullptr) {
		object->Release();
	}
}

// ---- Which programs are SLS2003's ----------------------------------------------

constexpr std::uint32_t kSlsVertexSize = 996;
constexpr std::uint32_t kSlsVertexHash = 0x55A67685u;
constexpr std::uint32_t kSlsPixelSize = 864;
constexpr std::uint32_t kSlsPixelHash = 0xC39FE354u;

std::uint32_t Fnv(const std::uint8_t* bytes, std::uint32_t count) {
	std::uint32_t h = 2166136261u;
	for (std::uint32_t i = 0; i < count; ++i) {
		h ^= bytes[i];
		h *= 16777619u;
	}
	return h;
}

// Whether a program object's bytecode has this size and hash.
template <class Shader>
bool CodeIs(Shader* shader, std::uint32_t size, std::uint32_t hash) {
	UINT have = 0;
	if (shader == nullptr || FAILED(shader->GetFunction(nullptr, &have)) || have != size) {
		return false;
	}
	std::uint8_t code[1024];
	return size <= sizeof(code) && SUCCEEDED(shader->GetFunction(code, &have)) && Fnv(code, size) == hash;
}

// Program objects seen, by identity: whether the object is SLS2003's. The
// engine makes its programs once, from the shader package, at start.
struct ProgramMemo {
	std::uint32_t object = 0;
	bool sls = false;
};
constexpr std::uint32_t kMemos = 128;
ProgramMemo g_vertexMemo[kMemos];
ProgramMemo g_pixelMemo[kMemos];

template <class Shader>
bool Memoised(ProgramMemo (&memo)[kMemos], Shader* shader, std::uint32_t size, std::uint32_t hash) {
	const std::uint32_t id = Identity(shader);
	if (id == 0) {
		return false;
	}
	const std::uint32_t start = (id >> 4) & (kMemos - 1);
	for (std::uint32_t probe = 0; probe < kMemos; ++probe) {
		ProgramMemo& m = memo[(start + probe) & (kMemos - 1)];
		if (m.object == id) {
			return m.sls;
		}
		if (m.object == 0) {
			m.object = id;
			m.sls = CodeIs(shader, size, hash);
			return m.sls;
		}
	}
	return CodeIs(shader, size, hash);  // the memo full: asked every time
}

// ---- OBNG's own objects ----------------------------------------------------------

constexpr std::uint32_t kInstanceStride = sizeof(InstanceData);  // four float4
constexpr std::uint32_t kInstanceCapacity = 16384;

IDirect3DDevice9* g_device = nullptr;
IDirect3DVertexShader9* g_vertexShader = nullptr;
IDirect3DPixelShader9* g_pixelShader = nullptr;
bool g_programsFailed = false;
IDirect3DVertexBuffer9* g_instances = nullptr;
std::uint32_t g_instanceNext = 0;

// The engine's declarations with the instance stream added, by the engine's
// declaration (a reference held, so that its address cannot come back as
// another one).
struct Declaration {
	IDirect3DVertexDeclaration9* original = nullptr;
	IDirect3DVertexDeclaration9* instanced = nullptr;
	std::uint32_t streams = 0;        // the original's streams, a bit each
	std::uint32_t instanceStream = 0;
};
constexpr std::uint32_t kDeclarations = 64;
Declaration g_declarations[kDeclarations];
std::uint32_t g_declarationCount = 0;

void ReleaseAll() {
	ReleaseIf(g_instances);
	g_instances = nullptr;
	g_instanceNext = 0;
	ReleaseIf(g_vertexShader);
	ReleaseIf(g_pixelShader);
	g_vertexShader = nullptr;
	g_pixelShader = nullptr;
	g_programsFailed = false;
	for (std::uint32_t i = 0; i < g_declarationCount; ++i) {
		ReleaseIf(g_declarations[i].instanced);
		ReleaseIf(g_declarations[i].original);
	}
	g_declarationCount = 0;
}

// The objects belong to one device; another device starts them anew.
bool EnsurePrograms(IDirect3DDevice9* device) {
	if (device != g_device) {
		ReleaseAll();
		g_device = device;
	}
	if (g_vertexShader != nullptr && g_pixelShader != nullptr) {
		return true;
	}
	if (g_programsFailed) {
		return false;
	}
	if (FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(shaders::kSLS2003InstancedVertex), &g_vertexShader)) ||
	    FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(shaders::kSLS2003InstancedPixel), &g_pixelShader))) {
		ReleaseIf(g_vertexShader);
		ReleaseIf(g_pixelShader);
		g_vertexShader = nullptr;
		g_pixelShader = nullptr;
		g_programsFailed = true;
		return false;
	}
	return true;
}

// The instanced declaration for the device's current one: its elements, then
// the instance rows as TEXCOORD5..7 on the first stream it does not use.
const Declaration* DeclarationFor(IDirect3DDevice9* device, IDirect3DVertexDeclaration9* original) {
	if (original == nullptr) {
		return nullptr;
	}
	for (std::uint32_t i = 0; i < g_declarationCount; ++i) {
		if (g_declarations[i].original == original) {
			return g_declarations[i].instanced != nullptr ? &g_declarations[i] : nullptr;
		}
	}
	if (g_declarationCount == kDeclarations) {
		return nullptr;
	}
	D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 5];
	UINT count = 0;
	Declaration& d = g_declarations[g_declarationCount++];
	original->AddRef();
	d.original = original;
	if (FAILED(original->GetDeclaration(nullptr, &count)) || count == 0 || count > MAXD3DDECLLENGTH + 1 ||
	    FAILED(original->GetDeclaration(elements, &count))) {
		return nullptr;
	}
	UINT n = count - 1;  // without D3DDECL_END
	std::uint32_t highest = 0;
	for (UINT i = 0; i < n; ++i) {
		if (elements[i].Usage == D3DDECLUSAGE_TEXCOORD && elements[i].UsageIndex >= 4) {
			return nullptr;  // the slots the instance rows take
		}
		d.streams |= 1u << elements[i].Stream;
		highest = elements[i].Stream > highest ? elements[i].Stream : highest;
	}
	d.instanceStream = highest + 1;
	if (d.instanceStream >= 16) {
		return nullptr;
	}
	for (BYTE row = 0; row < 4; ++row) {  // TEXCOORD5..7 the world rows, TEXCOORD4 the absolute translation and scale
		elements[n++] = D3DVERTEXELEMENT9{static_cast<WORD>(d.instanceStream), static_cast<WORD>(16 * row), D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT,
		                                  D3DDECLUSAGE_TEXCOORD, static_cast<BYTE>(row < 3 ? 5 + row : 4)};
	}
	elements[n] = D3DVERTEXELEMENT9{0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0};
	if (FAILED(device->CreateVertexDeclaration(elements, &d.instanced))) {
		d.instanced = nullptr;
		return nullptr;
	}
	return &d;
}

// ---- The device's Reset ------------------------------------------------------------

constexpr std::uint32_t kDeviceResetSlot = 16;  // IDirect3DDevice9::Reset (d3d9.h's method order)
using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* parameters);
ResetFn g_originalReset = nullptr;
bool g_resetTried = false;

HRESULT __stdcall HookedReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* parameters) {
	if (self == g_device) {
		ReleaseIf(g_instances);  // the default-pool buffer; the programs and declarations are not pool objects
		g_instances = nullptr;
		g_instanceNext = 0;
	}
	return g_originalReset(self, parameters);
}

}  // namespace

bool ReadDeviceState(std::uint32_t device, DeviceState* out) {
	IDirect3DDevice9* d = AsDevice(device);
	if (d == nullptr || FAILED(d->GetVertexShaderConstantF(0, &out->vs[0][0], kVertexRegisters)) ||
	    FAILED(d->GetPixelShaderConstantF(1, out->ps[0], 1)) || FAILED(d->GetPixelShaderConstantF(2, out->ps[1], 1)) ||
	    FAILED(d->GetPixelShaderConstantF(7, out->ps[2], 1))) {
		return false;
	}
	constexpr DWORD kStages[4] = {0, 1, 6, 7};
	for (int i = 0; i < 4; ++i) {
		IDirect3DBaseTexture9* texture = nullptr;
		d->GetTexture(kStages[i], &texture);
		out->textures[i] = Identity(texture);
		ReleaseIf(texture);
	}
	IDirect3DVertexShader9* vs = nullptr;
	IDirect3DPixelShader9* ps = nullptr;
	IDirect3DVertexDeclaration9* decl = nullptr;
	d->GetVertexShader(&vs);
	d->GetPixelShader(&ps);
	d->GetVertexDeclaration(&decl);
	out->vertexShader = Identity(vs);
	out->pixelShader = Identity(ps);
	out->declaration = Identity(decl);
	ReleaseIf(vs);
	ReleaseIf(ps);
	ReleaseIf(decl);
	return true;
}

bool ReadVertexRegisters(std::uint32_t device, float (&out)[kVertexRegisters][4]) {
	IDirect3DDevice9* d = AsDevice(device);
	return d != nullptr && SUCCEEDED(d->GetVertexShaderConstantF(0, &out[0][0], kVertexRegisters));
}

bool HoldsSls2003(std::uint32_t device) {
	IDirect3DDevice9* d = AsDevice(device);
	if (d == nullptr) {
		return false;
	}
	IDirect3DVertexShader9* vs = nullptr;
	IDirect3DPixelShader9* ps = nullptr;
	d->GetVertexShader(&vs);
	d->GetPixelShader(&ps);
	const bool sls = Memoised(g_vertexMemo, vs, kSlsVertexSize, kSlsVertexHash) && Memoised(g_pixelMemo, ps, kSlsPixelSize, kSlsPixelHash);
	ReleaseIf(vs);
	ReleaseIf(ps);
	return sls;
}

bool WriteInstances(std::uint32_t device, const InstanceData* rows, std::uint32_t count, std::uint32_t* first) {
	IDirect3DDevice9* d = AsDevice(device);
	if (d == nullptr || count == 0 || count > kInstanceCapacity || !EnsurePrograms(d)) {
		return false;
	}
	if (g_instances == nullptr) {
		if (FAILED(d->CreateVertexBuffer(kInstanceCapacity * kInstanceStride, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &g_instances, nullptr))) {
			g_instances = nullptr;
			return false;
		}
		g_instanceNext = kInstanceCapacity;  // the first lock discards
	}
	DWORD flags = D3DLOCK_NOOVERWRITE;
	if (g_instanceNext + count > kInstanceCapacity) {
		g_instanceNext = 0;
		flags = D3DLOCK_DISCARD;
	}
	void* data = nullptr;
	if (FAILED(g_instances->Lock(g_instanceNext * kInstanceStride, count * kInstanceStride, &data, flags)) || data == nullptr) {
		return false;
	}
	std::memcpy(data, rows, count * kInstanceStride);
	g_instances->Unlock();
	*first = g_instanceNext;
	g_instanceNext += count;
	return true;
}

bool DrawInstanced(std::uint32_t device, const Derived& derived, const float (&registers)[kVertexRegisters][4], float shadowMode, std::uint32_t first,
                   std::uint32_t count, const IndexedCall& call, Fallback* why) {
	IDirect3DDevice9* d = AsDevice(device);
	*why = Fallback::kDevice;
	if (d == nullptr || d != g_device || g_instances == nullptr || !EnsurePrograms(d)) {
		return false;
	}
	IDirect3DVertexDeclaration9* original = nullptr;
	d->GetVertexDeclaration(&original);
	const Declaration* decl = DeclarationFor(d, original);
	ReleaseIf(original);  // the table holds its own reference
	if (decl == nullptr) {
		return false;
	}
	IDirect3DVertexShader9* vs = nullptr;
	IDirect3DPixelShader9* ps = nullptr;
	d->GetVertexShader(&vs);
	d->GetPixelShader(&ps);
	d->SetVertexShaderConstantF(kRegisterViewProj, &derived.viewProj[0][0], 4);
	d->SetVertexShaderConstantF(kRegisterLight, derived.light, 1);
	d->SetVertexShaderConstantF(kRegisterShadow, &derived.shadow[0][0], 4);
	const float mode[4] = {shadowMode, 0.0f, 0.0f, 0.0f};
	d->SetVertexShaderConstantF(kRegisterShadowMode, mode, 1);
	d->SetVertexDeclaration(decl->instanced);
	d->SetVertexShader(g_vertexShader);
	d->SetPixelShader(g_pixelShader);
	for (UINT stream = 0; stream < 16; ++stream) {
		if ((decl->streams >> stream) & 1u) {
			d->SetStreamSourceFreq(stream, D3DSTREAMSOURCE_INDEXEDDATA | count);
		}
	}
	d->SetStreamSource(decl->instanceStream, g_instances, first * kInstanceStride, kInstanceStride);
	d->SetStreamSourceFreq(decl->instanceStream, D3DSTREAMSOURCE_INSTANCEDATA | 1u);
	const HRESULT drawn = d->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(call.type), call.base, call.minIndex, call.vertices, call.start, call.count);
	for (UINT stream = 0; stream < 16; ++stream) {
		if ((decl->streams >> stream) & 1u) {
			d->SetStreamSourceFreq(stream, 1);
		}
	}
	d->SetStreamSourceFreq(decl->instanceStream, 1);
	d->SetStreamSource(decl->instanceStream, nullptr, 0, 0);
	d->SetVertexDeclaration(decl->original);
	d->SetVertexShader(vs);
	d->SetPixelShader(ps);
	d->SetVertexShaderConstantF(0, &registers[0][0], kVertexRegisters);
	ReleaseIf(vs);
	ReleaseIf(ps);
	return SUCCEEDED(drawn);
}

// Once: a second hook on the same slot would call itself through the first
// (another hook that re-hooks whenever the slot is not its own loops the same way).
void EnsureDeviceResetHook(std::uint32_t device, LogFn log) {
	IDirect3DDevice9* d = AsDevice(device);
	if (g_resetTried || d == nullptr) {
		return;
	}
	g_resetTried = true;
	void** vtable = *reinterpret_cast<void***>(d);
	void* hook = reinterpret_cast<void*>(&HookedReset);
	g_originalReset = reinterpret_cast<ResetFn>(vtable[kDeviceResetSlot]);
	if (!SafeWrite(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&vtable[kDeviceResetSlot])), &hook, sizeof(hook))) {
		g_originalReset = nullptr;
		log("Route: draw instancing - the device's vtable at %p could not be written; a device reset with the instance buffer made will fail",
		    static_cast<void*>(vtable));
		return;
	}
	log("Route: draw instancing - Reset hooked on the device's vtable at %p (slot %u held %p)", static_cast<void*>(vtable), kDeviceResetSlot,
	    reinterpret_cast<void*>(g_originalReset));
}

}  // namespace obng::plugin::draw_instancing
