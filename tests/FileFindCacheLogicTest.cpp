// Every flow of FileFinder::FindFile's answers kept by path (src/engine/io/FileFindCacheLogic.h).

#include "Check.h"
#include "engine/io/FileFindCacheLogic.h"

#include <cstring>
#include <memory>
#include <string>

using namespace obng::engine::io::file_find_cache;

namespace {

Answer Found(const char* path, std::uint32_t result = 1) {
	Answer a;
	a.result = result;
	a.Keep(path);
	return a;
}

Answer NotFound() {
	Answer a;
	a.result = 0;
	a.Keep("");
	return a;
}

}  // namespace

int main() {
	Section("Which paths are kept: relative asset paths; absolute, dot-relative, empty, over-long and other kinds pass");
	Check(Classify("meshes\\architecture\\anvil\\house01.nif") == Pass::Keep, "a mesh");
	Check(Classify("textures\\landscape\\grass01.DDS") == Pass::Keep, "a texture, the extension in capitals");
	Check(Classify("DistantLOD\\Tamriel_-32_-4.lod") == Pass::Keep, "a far cell's LOD (0x004BD430's lookup)");
	Check(Classify("sound\\voice\\oblivion.esm\\imperial\\m\\line_1.mp3") == Pass::Keep, "a voice line");
	Check(Classify("meshes\\characters\\_male\\idle.kf") == Pass::Keep, "an animation");
	Check(Classify("C:\\Games\\Oblivion\\Data\\meshes\\a.nif") == Pass::Absolute, "a drive letter: absolute (the original sets flags 5)");
	Check(Classify(".\\Data\\meshes\\a.nif") == Pass::DotRelative, "a path from the game's folder (the original sets flags 4)");
	Check(Classify("") == Pass::Empty, "an empty path");
	Check(Classify(nullptr) == Pass::Empty, "no path at all");
	Check(Classify("Data\\OBSE\\Plugins\\SomeMod.ini") == Pass::Kind, "an INI: mods write them, so never kept");
	Check(Classify("Data\\SomeMod.txt") == Pass::Kind && Classify("Data\\log") == Pass::Kind, "a text file, a name without an extension");
	Check(Classify("meshes\\a.nif.bak") == Pass::Kind, "only the last extension counts");
	{
		std::string longPath(kPathMax - 1, 'a');
		longPath.replace(longPath.size() - 4, 4, ".nif");
		Check(Classify(longPath.c_str()) == Pass::Keep, "255 characters fit the original's 256-byte buffer");
		longPath.insert(0, "a");
		Check(Classify(longPath.c_str()) == Pass::TooLong, "256 characters do not: passed to the original as it is");
	}

	Section("The extension's kind");
	Check(KeptKind("nif") && KeptKind("NIF") && KeptKind("dds") && KeptKind("lod") && KeptKind("wav"), "asset kinds, any case");
	Check(!KeptKind("ini") && !KeptKind("") && !KeptKind("niff") && !KeptKind("ni"), "not an asset kind");
	Check(std::strcmp(ExtensionOf("a\\b.c\\d.NiF"), "NiF") == 0, "the extension after the last dot of the last name");
	Check(std::strcmp(ExtensionOf("a.b\\noext"), "") == 0, "a dot in a folder's name is no extension");

	Section("The key: the finder, the flags, the mask and the path's exact bytes");
	{
		KeyBuffer a, b, c, d, e;
		Check(a.Make(0x00B33A10, 0, 0xFFFF, "meshes\\a.nif") && b.Make(0x00B33A10, 0, 0xFFFF, "meshes\\a.nif"), "two keys made");
		Check(a.View() == b.View(), "the same call: the same key");
		Check(c.Make(0x00B33A10, 0, 0xFFFF, "Meshes\\a.nif") && c.View() != a.View(),
		      "another case: another key (the found path copies the caller's spelling)");
		Check(d.Make(0x00B33A14, 0, 0xFFFF, "meshes\\a.nif") && d.View() != a.View(), "another finder: another key");
		Check(e.Make(0x00B33A10, 2, 0xFFFF, "meshes\\a.nif") && e.View() != a.View(), "other flags: another key");
		Check(e.Make(0x00B33A10, 0, 0x00FF, "meshes\\a.nif") && e.View() != a.View(), "another mask: another key");
		std::string tooLong(kPathMax, 'x');
		Check(!e.Make(0x00B33A10, 0, 0xFFFF, tooLong.c_str()), "a path the buffer cannot hold makes no key");
	}

	Section("An answer kept: the result and the found path, written back as the original writes them");
	{
		const Answer found = Found("Data\\meshes\\a.nif", 1);
		char out[kPathMax];
		std::memset(out, 'z', sizeof(out));
		found.WriteTo(out);
		Check(std::strcmp(out, "Data\\meshes\\a.nif") == 0, "found: the found path copied with its terminator");
		const Answer missing = NotFound();
		std::memset(out, 'z', sizeof(out));
		missing.WriteTo(out);
		Check(out[0] == '\0' && out[1] == 'z', "not found: only the first byte set to zero, as 0x004310C9 does");
		found.WriteTo(nullptr);
		Check(true, "no buffer: nothing written");
		Check(found.Same(1, "Data\\meshes\\a.nif") && !found.Same(2, "Data\\meshes\\a.nif") && !found.Same(1, "Data\\meshes\\b.nif"),
		      "the same answer: the same result and the same found path");
		Check(missing.Same(0, "") && missing.Same(0, "whatever the buffer held"), "not found: the result alone compares (the buffer is not the answer)");
	}

	Section("The table: kept, found again, full");
	{
		auto table = std::make_unique<Table>(3);
		KeyBuffer k1, k2, k3, k4;
		k1.Make(1, 0, 0xFFFF, "meshes\\a.nif");
		k2.Make(1, 0, 0xFFFF, "meshes\\b.nif");
		k3.Make(1, 0, 0xFFFF, "meshes\\c.nif");
		k4.Make(1, 0, 0xFFFF, "meshes\\d.nif");
		Check(table->Find(k1.View()) == nullptr && table->Size() == 0, "an empty table finds nothing");
		Check(table->Keep(k1.View(), Found("Data\\meshes\\a.nif")) == Kept::New && table->Size() == 1, "the first answer kept");
		const Answer* a = table->Find(k1.View());
		Check(a != nullptr && a->Same(1, "Data\\meshes\\a.nif"), "found again with its answer");
		Check(table->Keep(k1.View(), NotFound()) == Kept::Already && table->Find(k1.View())->result == 1, "a second answer for a kept key changes nothing");
		Check(table->Keep(k2.View(), NotFound()) == Kept::New && table->Find(k2.View())->result == 0, "a not-found answer is kept too");
		Check(table->Keep(k3.View(), Found("x")) == Kept::New && table->Keep(k4.View(), Found("y")) == Kept::Full, "the fourth into a table of three: full");
		Check(table->Find(k4.View()) == nullptr && table->Size() == 3, "nothing kept past the limit");
	}

	Section("The counts by reason add up");
	{
		Counts c;
		c.Note(Pass::Keep);
		c.Note(Pass::Absolute);
		c.Note(Pass::DotRelative);
		c.Note(Pass::TooLong);
		c.Note(Pass::Kind);
		c.Note(Pass::Empty);
		Check(c.passed[static_cast<int>(Pass::Absolute)] == 1 && c.passed[static_cast<int>(Pass::Kind)] == 1 && c.passed[static_cast<int>(Pass::Keep)] == 0,
		      "a pass counted by its reason; a kept path is not a pass");
		Check(c.PassedAll() == 5, "five passes");
	}
	return Failures();
}
