// The grass fade fix against the game's own shaders: every vertex shader of
// a shader package (Data/Shaders/shaderpackageNNN.sdp of the installed
// game; not in Git, the game's files stay out of it) goes through
// PatchGrassFade. Every GRASS*.vso of version 2.0 or later must be patched,
// nothing else may be, every twin must read ModelViewProj's rows at c9, and
// every twin must walk as byte code and differ from its original by the
// patch at the dp4 only.
//
//   GrassFadePatchPackageTest [package.sdp] [--dump <directory>]
//
// Without an argument: $OBNG_SHADER_PACKAGE, else the Steam default under
// $HOME. No package there: the test says so and returns 77 (skipped).
// --dump writes each original and twin as <name> and <name>.twin, for a
// disassembler.
//
// The package's layout (read from the files: three dwords - 100, the entry
// count, the data size - then per entry a name of 0x100 bytes, a dword
// size and the byte code).

#include "Check.h"
#include "GrassFadePatchLogic.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace gf = obng::plugin::grass_fade;

namespace {

bool EndsWith(const std::string& s, const char* tail) {
	const std::size_t n = std::strlen(tail);
	return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

}  // namespace

int main(int argc, char** argv) {
	std::string path;
	std::string dump;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
			dump = argv[++i];
		} else {
			path = argv[i];
		}
	}
	if (path.empty()) {
		if (const char* env = std::getenv("OBNG_SHADER_PACKAGE")) {
			path = env;
		} else if (const char* home = std::getenv("HOME")) {
			path = std::string(home) + "/.local/share/Steam/steamapps/common/Oblivion/Data/Shaders/shaderpackage019.sdp";
		}
	}
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		std::printf("SKIPPED: no shader package at \"%s\" (give one as the argument or in OBNG_SHADER_PACKAGE)\n", path.c_str());
		return 77;
	}
	const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	Section("The package");
	std::uint32_t head[3] = {};
	Check(bytes.size() >= sizeof(head), "has a header");
	std::memcpy(head, bytes.data(), sizeof(head));
	std::printf("  %s: %u entries\n", path.c_str(), head[1]);

	std::uint32_t vertexShaders = 0;
	std::uint32_t grassNamed = 0;
	std::uint32_t grassVersion2 = 0;
	std::uint32_t patched = 0;
	std::uint32_t wronglyPatched = 0;
	std::uint32_t grassRefused = 0;
	std::uint32_t scaleNamed = 0;
	std::uint32_t badTwins = 0;
	std::uint32_t registers[64] = {};  // how many twins take each temp register as their first
	std::size_t at = sizeof(head);
	for (std::uint32_t entry = 0; entry < head[1] && at + 0x104 <= bytes.size(); ++entry) {
		const std::string name(bytes.data() + at, strnlen(bytes.data() + at, 0x100));
		std::uint32_t size = 0;
		std::memcpy(&size, bytes.data() + at + 0x100, sizeof(size));
		at += 0x104;
		if (at + size > bytes.size()) {
			break;
		}
		std::vector<std::uint32_t> code(size / 4);
		std::memcpy(code.data(), bytes.data() + at, code.size() * 4);
		at += size;
		if (!EndsWith(name, ".vso")) {
			continue;
		}
		++vertexShaders;
		std::vector<std::uint32_t> twin;
		const gf::Patch patch = gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kGame, &twin);
		const bool grass = name.rfind("GRASS", 0) == 0;
		grassNamed += grass ? 1u : 0u;
		grassVersion2 += grass && gf::IsVertexShader2OrLater(code.empty() ? 0 : code[0]) ? 1u : 0u;
		if (patch.verdict != gf::Verdict::kPatched) {
			if (grass && gf::IsVertexShader2OrLater(code.empty() ? 0 : code[0])) {
				++grassRefused;
				std::printf("  %s not patched: %s\n", name.c_str(), gf::VerdictName(patch.verdict));
			}
			continue;
		}
		if (!grass) {
			++wronglyPatched;
			std::printf("  %s patched though it is no grass shader\n", name.c_str());
			continue;
		}
		++patched;
		// The rows: in the game's grass shaders ModelViewProj is at c9.
		if (patch.rowRegister != 9) {
			++scaleNamed;
			std::printf("  %s: ModelViewProj read at c%u\n", name.c_str(), patch.rowRegister);
		}
		registers[patch.tempRegister < 64 ? patch.tempRegister : 63] += 1;
		// The twin: walks, four tokens more, equal outside the eight tokens at the patch.
		std::size_t end = 0;
		const bool walks = gf::ForInstructions(twin.data(), twin.size(), [&end](const gf::Instruction& in) { end = in.at + in.tokens; });
		std::size_t originalEnd = 0;
		gf::ForInstructions(code.data(), code.size(), [&originalEnd](const gf::Instruction& in) { originalEnd = in.at + in.tokens; });
		bool same = walks && end + 1 == twin.size() && twin.size() == originalEnd + 1 + patch.added && patch.added == 31;
		for (std::size_t i = 0; same && i < patch.at; ++i) {
			same = twin[i] == code[i];
		}
		for (std::size_t i = patch.at + 4; same && i <= originalEnd; ++i) {
			same = twin[i + patch.added] == code[i];
		}
		if (!same) {
			++badTwins;
			std::printf("  %s: the twin is not the original with the patch\n", name.c_str());
		}
		if (!dump.empty()) {
			std::ofstream(dump + "/" + name, std::ios::binary).write(reinterpret_cast<const char*>(code.data()), static_cast<std::streamsize>((originalEnd + 1) * 4));
			std::ofstream(dump + "/" + name + ".twin", std::ios::binary).write(reinterpret_cast<const char*>(twin.data()), static_cast<std::streamsize>(twin.size() * 4));
		}
	}
	std::printf("  %u vertex shaders, %u named GRASS* (%u of version 2.0 or later), %u patched\n", vertexShaders, grassNamed, grassVersion2, patched);
	Section("Every grass vertex shader of version 2.0 or later is patched, nothing else");
	Check(vertexShaders > 0, "the package has vertex shaders");
	Check(patched == grassVersion2 && grassRefused == 0, "every grass shader of version 2.0 or later patched (a package of 1.1 shaders has none)");
	Check(wronglyPatched == 0, "no other shader patched");
	for (std::uint32_t r = 0; r < 64; ++r) {
		if (registers[r] != 0) {
			std::printf("  temps r%u and r%u: %u twins\n", r, r + 1, registers[r]);
		}
	}
	Check(scaleNamed == 0, "every twin reads ModelViewProj's rows at c9");
	Check(badTwins == 0, "every twin is its original with the dp4 replaced by the patch's 35 tokens");
	return Failures();
}
