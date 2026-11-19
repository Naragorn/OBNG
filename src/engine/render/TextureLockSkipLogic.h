#pragma once
// The texture manager's prepare 0x0077AC90 without the renderer's lock for a texture that already has its renderer
// data - the decisions over plain values ([Route] TextureLockSkip in the plugin's TextureLockSkip.cpp).
//
// The prepare (read 2026-11-19 in the disassembly; NiDX9TextureManager vtable 0x00A8A9DC slot 2, __thiscall(texture,
// three bool out pointers), RET 0x10): enters the renderer's lock [[0x00B3F928]+0x180] at 0x0077ACC6, reads the
// texture's renderer data +0x24, creates it through 0x00761270 when it is null, leaves the lock at 0x0077AD16 and only
// then uses the data (0x0077AD1C on). The lock's other holder is the renderer's precache, vtable 0x00A88EA4 slot 65,
// 0x00763560, which runs on the loader's thread and holds it while it reads a texture file and runs D3DX: the render
// thread waited on it 326 ms a ride, up to 12 ms at once (measured, run 20261119-134406), mostly for textures that
// were ready long before.
//
// Class B: the data pointer is read without the lock. Every writer publishes it as its last step, after the texture's
// data is complete (read: 0x00761270 at 0x0076134B and 0x007615A8, 0x00774550 at its end), and the original itself
// uses the pointer after leaving the lock, so the lock never guarded its use - only two creations against each other,
// which a null pointer still goes through. The check mode reads the pointer both ways and compares.
//
// 64-bit clean: the pointer comes in as a 32-bit handle.
#include <cstdint>

namespace obng::engine::render::texture_lock_skip {

enum class Way : std::uint8_t {
	Unlocked,  // the data is there: straight on to its use, as the original does after its lock
	Locked,    // no data yet: the original's way, under the lock, which creates it
};

inline Way WayOf(std::uint32_t rendererData) {
	return rendererData != 0 ? Way::Unlocked : Way::Locked;
}

// The check: what the pointer read without the lock said, against what it reads under the lock right after. A null
// seen without the lock takes the lock anyway, so only a non-null one has to agree.
inline bool Agrees(std::uint32_t seenUnlocked, std::uint32_t seenLocked) {
	return seenUnlocked == 0 || seenUnlocked == seenLocked;
}

}  // namespace obng::engine::render::texture_lock_skip
