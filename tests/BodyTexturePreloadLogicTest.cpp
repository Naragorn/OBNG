// Every flow of the body texture preload's decisions (src/engine/render/BodyTexturePreloadLogic.h).

#include <cstring>

#include "Check.h"
#include "engine/render/BodyTexturePreloadLogic.h"

using namespace obng::engine::render::body_texture_preload;

int main() {
	Section("The INI's value");
	Check(ModeOf(0) == Mode::Off && ModeOf(1) == Mode::On && ModeOf(2) == Mode::Async, "0 off, 1 on, 2 async");
	Check(ModeOf(7) == Mode::Off && ModeOf(-1) == Mode::Off, "anything else: off");
	Check(WayOf(Mode::Off) == Way::None, "off: nothing preloaded");
	Check(WayOf(Mode::On) == Way::Preload, "on: preloaded");
	Check(WayOf(Mode::Async) == Way::Preload, "async is not built yet: preloaded as on");

	Section("Where a race's textures are");
	Check(EntryOffset(0) == 0x224 && EntryOffset(5) == 0x260 && EntryOffset(9) == 0x290, "entry p + 5 s, 12 bytes each");

	Section("The path");
	char out[32];
	Check(PathOf("Characters\\A.dds", 16, out, sizeof(out)) && std::strcmp(out, "Textures\\Characters\\A.dds") == 0, "the prefix put before it");
	Check(!PathOf(nullptr, 3, out, sizeof(out)), "no path");
	Check(!PathOf("x", 0, out, sizeof(out)), "an empty path");
	Check(!PathOf("Characters\\LongerThanTheBuffer.dds", 34, out, sizeof(out)), "a path that does not fit");
	Check(PathOf("0123456789012345678901", 22, out, sizeof(out)) && std::strlen(out) == 31, "exactly fitting, with its end");

	Section("The normal map beside a skin");
	char normal[64];
	Check(NormalPathOf("Textures\\Characters\\Breton\\Female\\FootFemale.dds", normal, sizeof(normal)) &&
	          std::strcmp(normal, "Data\\Textures\\Characters\\Breton\\Female\\FootFemale_n.dds") == 0,
	      "Data\\ before, _n before .dds");
	Check(NormalPathOf("Textures\\a.DDS", normal, sizeof(normal)) && std::strcmp(normal, "Data\\Textures\\a_n.dds") == 0, "an upper-case .DDS");
	Check(!NormalPathOf("Textures\\a.tga", normal, sizeof(normal)), "not a .dds: none");
	Check(!NormalPathOf("dds", normal, sizeof(normal)) && !NormalPathOf(nullptr, normal, sizeof(normal)), "too short or no path");
	char tight[22];  // "Data\\Textures\\a_n.dds" is 21 characters
	Check(!NormalPathOf("Textures\\ab.dds", tight, sizeof(tight)), "does not fit: none");
	Check(NormalPathOf("Textures\\a.dds", tight, sizeof(tight)) && std::strlen(tight) == 21, "exactly fitting");

	Section("The same file");
	Check(SameFile("Textures\\Characters\\Foot.dds", "textures/characters/FOOT.DDS"), "case and slashes do not count");
	Check(!SameFile("Textures\\a.dds", "Textures\\b.dds"), "another name");
	Check(!SameFile("Textures\\a.dds", "Textures\\a.dds2"), "a longer name");

	Section("The race paths changed since the last preload");
	const std::uint32_t a[] = {1, 2, 3};
	const std::uint32_t b[] = {1, 9, 3};
	Check(!Changed(a, 3, a, 3), "the same pointers: nothing to do");
	Check(Changed(a, 3, b, 3), "a path's pointer changed: preload again");
	Check(Changed(a, 3, a, 2), "another count (a race more or less): preload again");
	Check(Changed(nullptr, 0, a, 3), "no preload yet: preload");
	Check(!Changed(nullptr, 0, nullptr, 0), "no races and none before: nothing");
	return Failures();
}
