// Every flow of the sound files kept in memory and the sound folders' kept listings
// (src/engine/sound/SoundFileCacheLogic.h).

#include "Check.h"
#include "engine/sound/SoundFileCacheLogic.h"

#include <cstring>
#include <memory>
#include <string>

using namespace obng::engine::sound::file_cache;

namespace {

std::shared_ptr<const Bytes> File(std::size_t size, std::uint8_t first = 0) {
	auto bytes = std::make_shared<Bytes>(size);
	for (std::size_t i = 0; i < size; ++i) {
		(*bytes)[i] = static_cast<std::uint8_t>(first + i);
	}
	return bytes;
}

Names List(std::initializer_list<const char*> texts) {
	Names names;
	for (const char* text : texts) {
		names.push_back(text != nullptr ? Name{text, true} : Name{"", false});
	}
	return names;
}

}  // namespace

int main() {
	Section("The switch: 1 routes, 2 checks, anything else is off; off as shipped");
	Check(ModeOf(0) == Mode::Off, "0 is off");
	Check(ModeOf(1) == Mode::Route, "1 is the route");
	Check(ModeOf(2) == Mode::Check, "2 is the check");
	Check(ModeOf(3) == Mode::Off && ModeOf(-1) == Mode::Off, "another value is off");
	Check(ModeOf(Shipped()) == Mode::Off, "off as shipped");

	Section("The key: lower case, backslashes; no key for no path, an empty one or one too long");
	std::string key;
	Check(KeyOf("Data\\Sound\\FX\\UI\\Menu/UI_InventoryOpen.WAV", &key) && key == "data\\sound\\fx\\ui\\menu\\ui_inventoryopen.wav", "case and slashes folded");
	Check(!KeyOf(nullptr, &key), "no path");
	Check(!KeyOf("", &key), "an empty path");
	const std::string longest(kPathMax - 1, 'a');
	const std::string tooLong(kPathMax, 'a');
	Check(KeyOf(longest.c_str(), &key) && key.size() == kPathMax - 1, "a path that fills the buffer with its terminator");
	Check(!KeyOf(tooLong.c_str(), &key), "one byte more does not fit");

	Section("What an open does: the original when off or without a key, the kept bytes, a fill, or the original for a refused file");
	Check(PlanOf(Mode::Off, true, true, false) == Plan::Original, "off");
	Check(PlanOf(Mode::Route, false, false, false) == Plan::Original, "no key");
	Check(PlanOf(Mode::Route, true, true, false) == Plan::Serve, "kept: served");
	Check(PlanOf(Mode::Check, true, true, false) == Plan::Serve, "kept, in the check: served (the plugin keeps the game's file beside it)");
	Check(PlanOf(Mode::Route, true, false, false) == Plan::Fill, "not kept: read whole and kept");
	Check(PlanOf(Mode::Route, true, false, true) == Plan::Original, "found too large before: the game's file");
	Check(PlanOf(Mode::Route, true, true, true) == Plan::Serve, "kept wins over refused");

	Section("The store: a file is kept once, found again, and handed out alive");
	{
		Store store(100, 250, 8);
		Check(store.Find("a") == nullptr, "nothing kept yet");
		Check(store.Keep("a", File(100, 1)) == Kept::New, "a file of the largest size is kept");
		Check(store.Keep("a", File(10)) == Kept::Already, "the same key again is not replaced");
		const auto a = store.Find("a");
		Check(a != nullptr && a->size() == 100 && (*a)[0] == 1, "found with its bytes");
		Check(store.Files() == 1 && store.Total() == 100, "one file, its bytes counted");
		Check(store.Keep("big", File(101)) == Kept::TooLarge, "one byte over the file limit is refused");
		Check(store.Refused("big") && !store.Refused("a"), "and remembered as refused");
		Check(store.Fits(100) && !store.Fits(101), "Fits is the file limit");
	}

	Section("The store's budget: the least recently used file goes first, by bytes and by count");
	{
		Store store(100, 250, 8);
		store.Keep("a", File(100));
		store.Keep("b", File(100));
		Check(store.Find("a") != nullptr, "a is used after b");
		const auto held = store.Find("b");
		Check(store.Find("a") != nullptr, "and a once more: b is now the oldest");
		Check(store.Keep("c", File(100)) == Kept::New, "a third file of 100 does not fit 250 beside two");
		Check(store.Find("b") == nullptr && store.Find("a") != nullptr && store.Find("c") != nullptr, "b went, a and c stay");
		Check(store.Total() == 200 && store.Dropped() == 1, "the bytes and the drop counted");
		Check(held != nullptr && held->size() == 100, "the dropped file stays alive for its holder");
		Store few(100, 1000, 2);
		few.Keep("a", File(1));
		few.Keep("b", File(1));
		Check(few.Keep("c", File(1)) == Kept::New && few.Files() == 2 && few.Find("a") == nullptr, "the count limit drops the oldest too");
		Store small(100, 50, 8);
		Check(small.Keep("x", File(60)) == Kept::TooLarge && !small.Fits(60), "a file over the whole budget is refused");
	}

	Section("A read: the bytes from the place, short at the end, none after it; lDiskOffset grows by what was read");
	{
		const auto file = File(10, 100);
		Cursor cursor;
		std::uint8_t out[16] = {};
		Check(Read(*file, &cursor, out, 4, 0) == Answer{4, 4} && out[0] == 100 && out[3] == 103 && cursor.position == 4, "four bytes from the start");
		Check(Read(*file, &cursor, out, 0, 4) == Answer{0, 4} && cursor.position == 4, "none wanted");
		Check(Read(*file, &cursor, out, 16, 4) == Answer{6, 10} && out[0] == 104 && out[5] == 109 && cursor.position == 10, "short at the end");
		Check(Read(*file, &cursor, out, 4, 10) == Answer{0, 10}, "none at the end");
		cursor.position = 50;
		Check(Read(*file, &cursor, out, 4, 50) == Answer{0, 50} && cursor.position == 50, "none past the end");
		Check(Read(Bytes{}, &cursor, out, 4, 7) == Answer{0, 7}, "an empty file");
	}

	Section("A seek: from the start, the end or the place; answers 0 with the new place");
	{
		const auto file = File(10);
		Cursor cursor;
		Check(Seek(*file, &cursor, 6, 0) == Answer{kAnswerOk, 6}, "from the start");
		Check(Seek(*file, &cursor, 2, 1) == Answer{kAnswerOk, 8}, "from the place");
		Check(Seek(*file, &cursor, -3, 1) == Answer{kAnswerOk, 5}, "back from the place");
		Check(Seek(*file, &cursor, 0, 2) == Answer{kAnswerOk, 10}, "the end");
		Check(Seek(*file, &cursor, -4, 2) == Answer{kAnswerOk, 6}, "back from the end");
		Check(Seek(*file, &cursor, 1, 7) == Answer{kAnswerOk, 7}, "an unknown origin is the place (the game's default)");
		Check(Seek(*file, &cursor, 20, 0) == Answer{kAnswerOk, 20}, "past the end is kept (a read there gives nothing)");
		Check(Seek(*file, &cursor, -30, 1) == Answer{kAnswerOk, 0}, "before the start is the start");
		cursor.position = 0xFFFFFFF0u;
		Check(Seek(*file, &cursor, 0x7FFFFFFF, 1) == Answer{kAnswerOk, 0xFFFFFFFFu}, "the place does not wrap");
	}

	Section("A folder's pick as 0x006A9CD0 makes it: the random number modulo the named count, counted over every node");
	{
		const Names three = List({"a.wav", "b.wav", "c.wav"});
		Check(CountOf(three) == 3, "three named");
		Check(PickOf(three, 0) == 0 && PickOf(three, 1) == 1 && PickOf(three, 2) == 2 && PickOf(three, 3) == 0 && PickOf(three, 0xFFFFFFFFu) == 0, "modulo the count");
		const Names none = List({nullptr});
		Check(CountOf(none) == 0 && PickOf(none, 5) == kNoPick, "an empty list's head: nothing picked");
		Check(CountOf(Names{}) == 0 && PickOf(Names{}, 5) == kNoPick, "no nodes at all");
		const Names gap = List({"a.wav", nullptr, "c.wav"});
		Check(CountOf(gap) == 2, "a node without a name is not counted");
		Check(PickOf(gap, 0) == 0, "the walk counts it as a node: 0 is the first");
		Check(PickOf(gap, 1) == kNoPick, "and 1 reaches the node without a name (the game would read through null)");
		Check(PickOf(gap, 2) == 0, "2 modulo 2 named is the first again; the third is never reached");
	}

	Section("The listings by key, up to a count; two listings compare node for node");
	{
		Folders folders(2);
		Check(folders.Find("x") == nullptr, "nothing kept yet");
		const auto names = std::make_shared<const Names>(List({"a.wav", "b.wav"}));
		Check(folders.Keep("x", names) && folders.Find("x") == names && folders.Size() == 1, "kept and found");
		Check(folders.Keep("x", std::make_shared<const Names>(List({"z.wav"}))) && folders.Find("x") == names, "the same key again keeps the first");
		Check(folders.Keep("y", names) && !folders.Keep("z", names) && folders.Size() == 2, "full at the limit");
		Check(SameNames(List({"a.wav", "b.wav"}), *names), "the same names in the same order");
		Check(!SameNames(List({"b.wav", "a.wav"}), *names), "another order");
		Check(!SameNames(List({"a.wav"}), *names), "another length");
		Check(!SameNames(List({"a.wav", nullptr}), List({"a.wav", ""})), "a node without a name is not an empty name");
	}

	Section("The addresses and offsets read in the EXE");
	Check(kProcedureSite == 0x006AAC43 + 7, "the procedure's address is the last four bytes of the eleven-byte mov at 0x006AAC43");
	Check(kMessageRead == 0 && kMessageWrite == 1 && kMessageSeek == 2 && kMessageOpen == 3 && kMessageClose == 4, "mmio's messages");
	return Failures();
}
