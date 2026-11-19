// Every flow of the archives' loose file check 0x0042C1D0 answered from the listing of the loose files
// (src/engine/io/ArchiveLooseCheckLogic.h).

#include "Check.h"
#include "engine/io/ArchiveLooseCheckLogic.h"

#include <string>

using namespace obng::engine::io;
using archive_loose_check::Decide;
using archive_loose_check::Decision;
using archive_loose_check::kChecked;

namespace {

loose_file_list::Listing MakeListing() {
	loose_file_list::Listing l;
	l.Add("Data");
	l.Add("Data\\Meshes");
	l.Add("Data\\Meshes\\Clutter\\Loose.NIF");
	l.Add("Data\\Textures\\menus\\a.dds");
	l.Add("Data\\Sound\\voice.mp3");
	l.Add("Data\\Meshes\\Armor\\x.nif");
	l.Add("Data\\Meshes\\armor\\y.nif");
	l.MarkAmbiguous("Data\\Meshes\\armor");
	l.Add("Data\\Meshes\\folder.nif");  // a folder named like a mesh: _stat finds a folder too
	l.Add("Data\\Meshes\\folder.nif\\inner.nif");
	l.Seal();
	return l;
}

}  // namespace

int main() {
	const loose_file_list::Listing listing = MakeListing();

	Section("Nothing to do: the switch off, or the record asked before");
	Check(Decide(false, 0, "meshes\\clutter\\loose.nif", listing) == Decision::Skip, "archive invalidation off");
	Check(Decide(true, kChecked, "meshes\\clutter\\loose.nif", listing) == Decision::Skip, "the record checked before");
	Check(Decide(true, kChecked | 0x1234u, nullptr, listing) == Decision::Skip, "checked before, no path: still nothing");

	Section("No loose file of that name: the record marked checked, 0 - the _stat would have failed");
	Check(Decide(true, 0, "meshes\\clutter\\other.nif", listing) == Decision::NoLooseFile, "a mesh not listed");
	Check(Decide(true, 0x7FFFFFFFu, "textures\\menus\\b.dds", listing) == Decision::NoLooseFile, "other flag bits do not matter");
	Check(Decide(true, 0, nullptr, listing) == Decision::NoLooseFile, "no path: the original marks it too");

	Section("A loose file or folder by that name: the original asks its time");
	Check(Decide(true, 0, "meshes\\clutter\\loose.nif", listing) == Decision::Loose, "the listed file, in other case");
	Check(Decide(true, 0, "Meshes/Clutter/LOOSE.nif", listing) == Decision::Loose, "'/' and upper case");
	Check(Decide(true, 0, "sound\\voice.mp3", listing) == Decision::Loose, "a sound");
	Check(Decide(true, 0, "meshes\\folder.nif", listing) == Decision::Loose, "a folder with the name: _stat succeeds on a folder");

	Section("Refused: the listing cannot say, the original answers");
	{
		loose_file_list::Listing unsealed;
		unsealed.Add("Data\\Meshes\\a.nif");
		Check(Decide(true, 0, "meshes\\b.nif", unsealed) == Decision::Refused, "the listing not ready");
	}
	Check(Decide(true, 0, "meshes\\a.xml", listing) == Decision::Refused && Decide(true, 0, "meshes\\noext", listing) == Decision::Refused,
	      "a kind the listing does not answer for");
	Check(Decide(true, 0, "", listing) == Decision::Refused, "an empty path");
	Check(Decide(true, 0, "meshes\\..\\a.nif", listing) == Decision::Refused && Decide(true, 0, "meshes\\\\a.nif", listing) == Decision::Refused,
	      "a path Windows would rewrite");
	Check(Decide(true, 0, "meshes\\armor\\y.nif", listing) == Decision::Refused, "under a case twin");
	Check(Decide(true, 0, "meshes\\armorx\\y.nif", listing) == Decision::NoLooseFile, "beside a case twin: answered");
	{
		std::string fits(loose_file_list::kKeyMax - 6 - 4, 'a');  // "Data\" + name + ".nif" + terminator = 256
		fits += ".nif";
		Check(Decide(true, 0, fits.c_str(), listing) == Decision::NoLooseFile, "the longest path that fits the key");
		std::string over = std::string(1, 'a') + fits;
		Check(Decide(true, 0, over.c_str(), listing) == Decision::Refused, "one byte longer: refused (the original's own buffer is 260)");
	}

	Section("What the route does with a decision");
	Check(archive_loose_check::AnswerOf(Decision::Skip).answered && !archive_loose_check::AnswerOf(Decision::Skip).mark, "skip: 0, nothing written");
	Check(archive_loose_check::AnswerOf(Decision::NoLooseFile).answered && archive_loose_check::AnswerOf(Decision::NoLooseFile).mark,
	      "no loose file: 0, the record marked");
	Check(!archive_loose_check::AnswerOf(Decision::Loose).answered && !archive_loose_check::AnswerOf(Decision::Refused).answered,
	      "loose or refused: the original");
	return Failures();
}
