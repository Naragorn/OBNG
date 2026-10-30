#pragma once

// The game side the range walk's two detours share - the shadow
// (ShadowRangeWalk.cpp) and the route (RouteRangeWalk.cpp): the entries'
// bytes, the 32-bit call shapes, the memory port over the game's own memory,
// the square root port and the per-thread slot. Moved here unchanged from
// ShadowRangeWalk.cpp, whose in-game gate ran over exactly these ports
// (artifacts/shadow/obng-range-walk/20261030-120624), plus the route's one
// write (W32).

#include <windows.h>

#include <cstddef>
#include <cstdint>

#include "ShadowRangeWalkLogic.h"
#include "Types.h"
#include "engine/render/ShadowSceneLight_IsInRange.h"

namespace obng::plugin::range_walk_game {

namespace rw = range_walk;
namespace walk_engine = engine::render::shadow_light_range_walk::engine;
namespace in_range_engine = engine::render::shadow_scene_light_is_in_range::engine;

// The logic's offsets against the IsInRange rebuild's views (both read in
// re/functions/0x007D2EA0.md; the views carry the instruction addresses).
static_assert(rw::kSourcePositionOffset == offsetof(in_range_engine::NiLightView, worldTranslate), "source +0x88");
static_assert(rw::kSourceRangeOffset == offsetof(in_range_engine::NiLightView, range), "source +0xF8");
static_assert(walk_engine::kPlaneSourceOffset == offsetof(in_range_engine::ShadowSceneLightView, planeSource), "+0x14C");
static_assert(walk_engine::kPlanesOffset == offsetof(in_range_engine::ShadowSceneLightView, planes), "+0x150");
static_assert(walk_engine::kActivePlanesOffset == offsetof(in_range_engine::ShadowSceneLightView, activePlanes), "+0x1B0");
static_assert(rw::kPlaneStride == sizeof(in_range_engine::NiPlane), "16-byte planes");
static_assert(walk_engine::kWorldBoundOffset == offsetof(in_range_engine::NiAVObjectView, worldBound), "+0x20");

// The entries (re/export/range-callers/*/asm.s and bytes.bin): whole
// instructions, nothing relative. The walk's first two instructions are 8
// bytes; no jump in its body targets 0x007D59E1..0x007D59E7 (its asm.s), its
// callers enter at 0x007D59E0 (meta.json). AddGeometry's first two are 7
// bytes (its SEH frame setup), no jump in its body targets inside them.
inline constexpr UInt32 kWalk = walk_engine::kCollectGeometryInRangeAddress;
inline constexpr UInt8 kWalkBytes[] = {0x51, 0x83, 0x3D, 0x48, 0x2F, 0xB4, 0x00, 0x00};  // push ecx; cmp dword [0x00B42F48],0
inline constexpr UInt32 kAddGeometry = walk_engine::kAddGeometryAddress;
inline constexpr UInt8 kAddGeometryBytes[] = {0x6A, 0xFF, 0x68, 0xB8, 0xEB, 0x9C, 0x00};  // push -1; push 0x009CEBB8

// __fastcall with a dummy edx mirrors __thiscall: ECX is this, the one stack
// argument cleaned by the callee (RET 4 in both originals, read). The walk's
// result is AL with the upper bits undefined (C1); EAX is passed on whole.
using WalkFn = UInt32(__fastcall*)(UInt32 light, void* edx, UInt32 object);
using AddGeometryFn = UInt32(__fastcall*)(UInt32 light, void* edx, UInt32 geometry);
// A thiscall without a stack argument: the virtual slots +0x04/+0x08/+0x10
// (called with ECX = the object and nothing pushed, 0x007D5A20, 0x007D5A35,
// 0x007D5AEA) and the helpers 0x007C59E0 / 0x0049FFC0 (plain RET, read in
// their asm.s).
using ThisFn = UInt32(__fastcall*)(UInt32 self, void* edx);

template <class T>
T Read(UInt32 address) {
	return *reinterpret_cast<volatile const T*>(address);
}

// The memory port of ShadowRangeWalkLogic.h / RouteRangeWalkLogic.h over the
// game's own memory.
struct GameMemory {
	std::uint8_t U8(std::uint32_t address) { return Read<std::uint8_t>(address); }
	std::uint16_t U16(std::uint32_t address) { return Read<std::uint16_t>(address); }
	std::uint32_t U32(std::uint32_t address) { return Read<std::uint32_t>(address); }
	float F32(std::uint32_t address) { return Read<float>(address); }
	void W32(std::uint32_t address, std::uint32_t value) { *reinterpret_cast<volatile UInt32*>(address) = value; }
	std::uint32_t CallVirtual(std::uint32_t object, std::uint32_t slot) {
		const UInt32 vtable = Read<UInt32>(object);
		return reinterpret_cast<ThisFn>(Read<UInt32>(vtable + slot))(object, nullptr);
	}
	std::uint32_t CallThis(std::uint32_t function, std::uint32_t self) {
		return reinterpret_cast<ThisFn>(function)(self, nullptr);
	}
};

// The square root IsInRange's distance path calls, 0x00982C30, run by the
// game itself (RangeWalkGame.cpp). `area` is 512 bytes, 16-aligned: the
// caller's x87/SSE state is saved there (FXSAVE), the x87 is reset (FNINIT)
// and given the caller's control word back (FLDCW from the image's FCW, so
// the precision control and rounding stay the game's), 0x00982C30 runs on
// that empty stack, the result is stored to *out, and the caller's state is
// restored (FXRSTOR). *out is the result stored as a float, the original's
// own store at 0x007D2FB0. So the call needs no free slot of the caller's x87
// stack and leaves it, its tags and its sticky flags as it found them.
void __cdecl GameSquareRootGuarded(float s, float* out, void* area);

// Copies the x87/SSE state into `area` (FXSAVE; 512 bytes, 16-aligned)
// without changing it.
void __cdecl CaptureX87(void* area);

// A 512-byte FXSAVE area, 16-aligned inside a plain stack buffer (aligning by
// hand, so nothing rests on the compiler realigning the frame).
struct FxsaveArea {
	unsigned char raw[512 + 15];
	void* Aligned() { return reinterpret_cast<void*>((reinterpret_cast<std::uintptr_t>(raw) + 15) & ~std::uintptr_t{15}); }
};

// The square-root port both walks take (ShadowRangeWalkLogic.h, the engine's
// Distance): forced inline, so the double comes back through memory and SSE,
// never through ST0 - a return in ST0 would push onto the caller's x87 stack
// again after the restore.
struct GameSquareRootPort {
	__forceinline double operator()(float s) const {
		FxsaveArea area;
		float out;
		GameSquareRootGuarded(s, &out, area.Aligned());
		return static_cast<double>(out);  // exact; Distance's StoreSingle then keeps it as it is
	}
};

// One T per thread in an explicit TLS slot (TlsAlloc) rather than
// thread_local, so nothing rests on the loader giving a LoadLibrary'd DLL
// implicit TLS. A thread's T is allocated by its first Get(). TlsGetValue
// sets the thread's last error on success; the game's is kept.
template <class T>
class ThreadSlots {
public:
	bool Allocate() {
		index_ = TlsAlloc();
		return index_ != TLS_OUT_OF_INDEXES;
	}
	T* Existing() const {
		const DWORD lastError = GetLastError();
		auto* slot = static_cast<T*>(TlsGetValue(index_));
		SetLastError(lastError);
		return slot;
	}
	T& Get() {
		T* slot = Existing();
		if (slot == nullptr) {
			const DWORD lastError = GetLastError();
			slot = new T();
			TlsSetValue(index_, slot);
			SetLastError(lastError);
		}
		return *slot;
	}

private:
	DWORD index_ = TLS_OUT_OF_INDEXES;
};

}  // namespace obng::plugin::range_walk_game
