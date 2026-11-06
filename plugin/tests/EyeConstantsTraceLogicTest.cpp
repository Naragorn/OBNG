// The eye constants trace's decisions (plugin/src/EyeConstantsTraceLogic.h):
// which registers differ between two snapshots, how the set is written, and
// which traces get a line.

#include "Check.h"
#include "EyeConstantsTraceLogic.h"

#include <cmath>
#include <cstring>
#include <string>

namespace et = obng::plugin::eye_trace;

namespace {

std::string Registers(std::uint64_t mask) {
	char text[200];
	et::FormatRegisters(mask, text, sizeof(text));
	return text;
}

void TestChangedRegisters() {
	Section("ChangedRegisters: a bit per register whose four floats differ");
	float a[et::kRegisters * 4] = {};
	float b[et::kRegisters * 4] = {};
	Check(et::ChangedRegisters(a, b) == 0, "equal snapshots: nothing");
	b[9 * 4 + 3] = 4.5f;
	b[12 * 4] = -1.0f;
	Check(et::ChangedRegisters(a, b) == ((std::uint64_t{1} << 9) | (std::uint64_t{1} << 12)), "one float of a register is enough");
	b[47 * 4 + 1] = 1.0f;
	Check((et::ChangedRegisters(a, b) >> 47 & 1u) == 1, "the last register counts");
	Check(et::ChangedRegisters(a, b, 10) == (std::uint64_t{1} << 9), "only the registers asked for are compared");
	a[0] = std::nanf("");
	b[0] = a[0];
	Check((et::ChangedRegisters(a, b) & 1u) == 0, "the same NaN is no change (bits are compared)");
	a[4] = 0.0f;
	b[4] = -0.0f;
	Check((et::ChangedRegisters(a, b) >> 1 & 1u) == 1, "a sign of zero is a change (bits are compared)");
}

void TestFormatRegisters() {
	Section("FormatRegisters: runs as ranges, single registers alone, none for the empty set");
	Check(Registers(0) == "none", "empty");
	Check(Registers(std::uint64_t{1} << 5) == "c5", "one register");
	Check(Registers(0xFull << 9) == "c9-c12", "a run");
	Check(Registers((0xFull << 9) | (std::uint64_t{1} << 15) | 3u) == "c0-c1 c9-c12 c15", "runs and singles in order");
	Check(Registers(std::uint64_t{1} << 63 | std::uint64_t{1} << 62) == "c62-c63", "the top of the mask");
	char small[8];
	const int used = et::FormatRegisters((0xFull << 9) | (std::uint64_t{1} << 15), small, sizeof(small));
	Check(std::string(small).substr(0, 6) == "c9-c12" && used < static_cast<int>(sizeof(small)), "a buffer too small keeps what fits whole");
}

void TestClassBook() {
	Section("ClassBook: the first trace of a class is written, later ones only when they show a fault, a few each");
	et::ClassBook book;
	Check(book.Note(0x00A9202C, true, true), "first of a class");
	Check(!book.Note(0x00A9202C, true, true), "a second good one is not written");
	Check(book.Note(0x00A9202C, false, true) && book.Note(0x00A9202C, false, true) && book.Note(0x00A9202C, false, true),
	      "three unchanged ones are written");
	Check(!book.Note(0x00A9202C, false, true), "the fourth is counted only");
	Check(book.Note(0x00A9202C, true, false) && book.Note(0x00A9202C, true, false) && book.Note(0x00A9202C, true, false) &&
	          !book.Note(0x00A9202C, true, false),
	      "the same for traces not restored, with their own count");
	const et::ClassBook::Row* row = book.Find(0x00A9202C);
	Check(row != nullptr && row->traced == 10 && row->unchanged == 4 && row->notRestored == 4, "every trace is counted");
	Check(book.Note(0x00A90634, false, false), "the first of another class, faulty both ways, is one line");
	Check(book.Find(0x00A90634)->unchanged == 1 && book.Find(0x00A90634)->notRestored == 1, "and counts both faults");
	Check(book.Find(0x12345678) == nullptr, "an unknown class has no row");

	Section("ClassBook: a class beyond the table's room is neither counted nor written");
	et::ClassBook full;
	for (std::uint32_t i = 0; i < et::ClassBook::kMaxClasses; ++i) {
		Check(full.Note(0x1000 + i, true, true), "a class within the room");
	}
	Check(!full.Note(0x9999, false, false) && full.Find(0x9999) == nullptr && full.size == et::ClassBook::kMaxClasses, "one too many");
}

}  // namespace

int main() {
	TestChangedRegisters();
	TestFormatRegisters();
	TestClassBook();
	return Failures();
}
