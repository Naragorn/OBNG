// Every flow of FileFinder's search slot 0x00431800 answered from a listing of the loose files made once
// (src/engine/io/LooseFileListLogic.h).

#include "Check.h"
#include "engine/io/LooseFileListLogic.h"

#include <cstring>
#include <string>
#include <vector>

using namespace obng::engine::io::loose_file_list;

namespace {

// A stand-in for the EXE's _strnicmp 0x009864D9: ASCII, case-insensitive, the first n bytes.
int Strnicmp(const char* a, const char* b, std::size_t n) {
	for (std::size_t i = 0; i < n; ++i) {
		const char x = Lower(a[i]);
		const char y = Lower(b[i]);
		if (x != y) {
			return x < y ? -1 : 1;
		}
		if (x == '\0') {
			return 0;
		}
	}
	return 0;
}

struct Fake {
	std::vector<std::string> archived;
	mutable int archiveCalls = 0;
	bool InArchive(const char* path) const {
		++archiveCalls;
		for (const std::string& a : archived) {
			if (a == path) {
				return true;
			}
		}
		return false;
	}
};

Listing MakeListing() {
	Listing l;
	l.Add("Data");
	l.Add("Data\\Meshes");
	l.Add("Data\\Meshes\\Anvil\\House01.NIF");
	l.Add("Data\\DistantLOD\\Tamriel_-32_-4.lod");
	l.Add("Data\\Textures\\loose.dds");
	l.Add("Oblivion.ini");
	l.Add("readme.nif");  // a kept kind in the game's own folder, found without a search folder
	l.Seal();
	return l;
}

}  // namespace

int main() {
	Section("The key: lower case, '/' as '\\'; anything Windows would rewrite is refused");
	{
		char key[kKeyMax];
		Check(MakeKey("Data\\Meshes\\A.NIF", key, sizeof(key)) && std::strcmp(key, "data\\meshes\\a.nif") == 0, "lower case");
		Check(MakeKey("Data/Meshes/a.nif", key, sizeof(key)) && std::strcmp(key, "data\\meshes\\a.nif") == 0, "'/' read as '\\'");
		Check(!MakeKey("", key, sizeof(key)) && !MakeKey(nullptr, key, sizeof(key)), "empty or no path");
		Check(!MakeKey("C:\\Games\\a.nif", key, sizeof(key)), "a drive letter");
		Check(!MakeKey("\\Data\\a.nif", key, sizeof(key)) && !MakeKey("\\\\server\\a.nif", key, sizeof(key)), "rooted or a share");
		Check(!MakeKey("Data\\\\a.nif", key, sizeof(key)) && !MakeKey("Data\\a\\", key, sizeof(key)), "a doubled or trailing separator");
		Check(!MakeKey("Data\\..\\a.nif", key, sizeof(key)) && !MakeKey(".\\a.nif", key, sizeof(key)) && !MakeKey("Data\\x.\\a.nif", key, sizeof(key)),
		      "a '.' or '..' part, or a name ending in '.'");
		Check(!MakeKey("Data\\a.nif ", key, sizeof(key)) && MakeKey("Data\\a .nif", key, sizeof(key)), "a name ending in a space is refused; an inner space is fine");
		Check(!MakeKey("Data\\\xE4.nif", key, sizeof(key)), "a byte over 0x7F (the code page's case rules are not ASCII's)");
		Check(!MakeKey("Data\\a?.nif", key, sizeof(key)) && !MakeKey("Data\\a*.nif", key, sizeof(key)) && !MakeKey("Data\\a\".nif", key, sizeof(key)) &&
		          !MakeKey("Data\\a<b>.nif", key, sizeof(key)) && !MakeKey("Data\\a|.nif", key, sizeof(key)) && !MakeKey("Data\\a:b.nif", key, sizeof(key)),
		      "wildcards and characters Windows forbids");
		char small[8];
		Check(!MakeKey("Data\\a.nif", small, sizeof(small)), "longer than the key's room");
	}

	Section("The listing: sealed before it answers, case-blind");
	{
		Listing l;
		l.Add("Data\\a.nif");
		Check(!l.Has("data\\a.nif"), "not sealed: nothing found");
		l.Seal();
		Check(l.Has("data\\a.nif") && !l.Has("data\\b.nif"), "sealed: a listed key found, another not");
		Check(l.Size() == 1, "one entry");
		Listing m;
		m.Add("Data\\a.nif");
		m.Add("DATA\\A.NIF");
		m.Seal();
		Check(m.Size() == 1, "the same file twice in other case: one entry");
	}

	Section("Two names in one folder that differ only in case (possible on Linux, not on NTFS): Wine sees one, so the listing answers for neither");
	{
		const std::vector<std::string> names = {"Armor", "Clothes", "armor", "x.nif", "X.NIF", "y.nif"};
		const std::vector<std::string> twins = CaseTwins(names);
		Check(twins.size() == 2 && twins[0] == "armor" && twins[1] == "x.nif", "each twin once, by its key");
		Check(CaseTwins(std::vector<std::string>{"a", "b"}).empty(), "no twins");
		Listing l;
		l.Add("Data\\Meshes\\Armor\\fur\\f\\a.nif");
		l.Add("Data\\Meshes\\armor\\fur\\f\\b.nif");
		l.MarkAmbiguous("Data\\Meshes\\armor");
		l.Add("Data\\Meshes\\Clothes\\c.nif");
		l.Seal();
		Check(l.Ambiguous("data\\meshes\\armor") && l.Ambiguous("data\\meshes\\armor\\fur\\f\\b.nif"), "the twin and everything under it");
		Check(!l.Ambiguous("data\\meshes\\clothes\\c.nif") && !l.Ambiguous("data\\meshes\\armorx\\a.nif") && !l.Ambiguous("data\\meshes"),
		      "a sibling, a longer name with the same start, the parent: not ambiguous");
		Fake fake;
		auto archive = [&fake](const char* p) { return fake.InArchive(p); };
		const char* data[] = {"Data\\"};
		const Folders f{data, 1};
		std::string combined;
		Check(Search("Meshes\\Armor\\Fur\\F\\b.nif", 0, archive, Strnicmp, f, l, &combined) == Outcome::Refused && fake.archiveCalls == 0,
		      "a call under a twin: refused before the archives are asked");
		Check(Search("Meshes\\Clothes\\c.nif", 0, archive, Strnicmp, f, l, &combined) == Outcome::LooseInFolder, "a call elsewhere: answered");
	}

	Section("Which calls the listing answers: kept asset kinds only");
	Check(Eligible("Data\\Meshes\\a.nif") && Eligible("DistantLOD\\Tamriel_-32_-4.lod"), "a mesh, a far cell's LOD");
	Check(!Eligible("Data\\OBSE\\Plugins\\x.ini") && !Eligible("Data\\a.txt") && !Eligible("Data\\noext"), "an INI, a text, no extension: asked of the OS");
	Check(!Eligible("C:\\a.nif") && !Eligible(nullptr), "an absolute path or none");

	const Listing listing = MakeListing();
	const char* folderNames[] = {"Data\\"};
	const Folders folders{folderNames, 1};

	Section("The search, as 0x00431800 does it");
	{
		Fake fake;
		fake.archived = {"meshes\\armor\\a.nif"};
		auto archive = [&fake](const char* p) { return fake.InArchive(p); };
		std::string combined;
		Check(Search("meshes\\armor\\a.nif", 0, archive, Strnicmp, folders, listing, &combined) == Outcome::Archive && combined.empty(),
		      "in an archive: 2, the path untouched");
		Check(Search("meshes\\armor\\a.nif", 1, archive, Strnicmp, folders, listing, &combined) == Outcome::NotFound, "flags 1: the archives skipped");
		Check(Search("readme.nif", 0, archive, Strnicmp, folders, listing, &combined) == Outcome::Loose && combined.empty(),
		      "loose in the game's own folder: 1, the path untouched");
		Check(Search("readme.nif", 2, archive, Strnicmp, folders, listing, &combined) == Outcome::NotFound, "flags 2: the game's own folder skipped");
		Check(Search("Meshes\\anvil\\house01.nif", 0, archive, Strnicmp, folders, listing, &combined) == Outcome::LooseInFolder &&
		          combined == "Data\\Meshes\\anvil\\house01.nif",
		      "loose under a search folder: 1, the folder and the path as the caller spelled it");
		Check(Search("Meshes\\anvil\\house01.nif", 4, archive, Strnicmp, folders, listing, &combined) == Outcome::NotFound, "flags 4: no search folders");
		Check(Search("Data\\Meshes\\anvil\\house01.nif", 0, archive, Strnicmp, folders, listing, &combined) == Outcome::Loose && combined.empty(),
		      "a path that already starts with the folder: found as it is");
		Check(Search("data\\Textures\\gone.dds", 0, archive, Strnicmp, folders, listing, &combined) == Outcome::NotFound,
		      "a path that starts with the folder is not searched under it again");
		Check(Search("DistantLOD\\Tamriel_-99_-99.lod", 0, archive, Strnicmp, folders, listing, &combined) == Outcome::NotFound, "nowhere: 0");
		const char* two[] = {"Data\\", "Data\\Textures\\"};
		const Folders twoFolders{two, 2};
		Check(Search("loose.dds", 0, archive, Strnicmp, twoFolders, listing, &combined) == Outcome::LooseInFolder && combined == "Data\\Textures\\loose.dds",
		      "the second folder when the first has nothing");
		Check(listing.Has("data\\meshes"), "a folder is listed: _access finds a folder as it finds a file");
	}

	Section("Refusals: the original answers the whole call");
	{
		Fake fake;
		auto archive = [&fake](const char* p) { return fake.InArchive(p); };
		std::string combined;
		Check(Search("Data\\a.ini", 0, archive, Strnicmp, folders, listing, &combined) == Outcome::Refused && fake.archiveCalls == 0,
		      "not a kept kind: refused before the archives are asked");
		Check(Search("meshes\\..\\a.nif", 0, archive, Strnicmp, folders, listing, &combined) == Outcome::Refused && fake.archiveCalls == 0, "a path Windows would rewrite");
		std::string longName(kCombinedMax - 5, 'a');
		longName += ".nif";
		Check(Search(longName.c_str(), 0, archive, Strnicmp, folders, listing, &combined) == Outcome::Refused,
		      "the folder and the path past 0x00431800's buffer: refused (the original's own overflow is not copied)");
		const char* bad[] = {"Data\\\\"};
		const Folders badFolders{bad, 1};
		Check(Search("meshes\\a.nif", 0, archive, Strnicmp, badFolders, listing, &combined) == Outcome::Refused, "a search folder that makes no key");
		Listing unsealed;
		Check(Search("meshes\\a.nif", 0, archive, Strnicmp, folders, unsealed, &combined) == Outcome::Refused, "no listing yet");
	}

	Section("The outcome as the slot returns it");
	Check(SlotResult(Outcome::Archive) == 2 && SlotResult(Outcome::Loose) == 1 && SlotResult(Outcome::LooseInFolder) == 1 && SlotResult(Outcome::NotFound) == 0,
	      "2 an archive, 1 loose, 0 not found");
	return Failures();
}
