// Every flow of the light sum's clip (src/engine/render/Lighting30ClipLogic.h): the key, the bytecode's fields, the
// constant table's lookup, the rule that finds the add which completes "ambient plus lights" in the shapes the
// one-pass programs have (plain, glow, another temporary, either order of the add's sources), everything it must
// leave alone, streams that do not hold together, and the counts. The programs here are built token by token; the
// game's own are tried with tools/re/sm3-clip.py.

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "Check.h"
#include "engine/render/Lighting30ClipLogic.h"

using namespace obng::engine::render::lighting30_clip;
using namespace obng::test;

namespace {

using Tokens = std::vector<std::uint32_t>;

constexpr std::uint32_t kPs2x = 0xFFFF0201, kPs30 = 0xFFFF0300, kVs20 = 0xFFFE0200, kPs14 = 0xFFFF0104;
constexpr std::uint32_t kMad = 4, kMul = 5, kMov = 1;
constexpr std::uint32_t kInput = 1, kSampler = 10;
constexpr std::uint32_t kXyz = 7, kW = 8;
constexpr std::uint32_t kPartial = 1u << 21;

std::uint32_t TypeBits(std::uint32_t type) {
	return ((type & 7) << 28) | ((type & 0x18) << 8);
}

std::uint32_t Dst(std::uint32_t type, std::uint32_t number, std::uint32_t mask = kXyz, std::uint32_t modifiers = kPartial) {
	return 0x80000000u | TypeBits(type) | number | (mask << 16) | modifiers;
}

std::uint32_t Src(std::uint32_t type, std::uint32_t number) {
	return 0x80000000u | TypeBits(type) | number | (0xE4u << 16);
}

void Put(Tokens& to, std::uint32_t op, std::initializer_list<std::uint32_t> parameters) {
	to.push_back(op | (static_cast<std::uint32_t>(parameters.size()) << 24));
	to.insert(to.end(), parameters);
}

// A constant table as the compiler writes it: the header, a record a constant, the names.
Tokens Table(const std::vector<std::pair<std::string, std::pair<std::uint32_t, std::uint32_t>>>& constants) {
	std::vector<unsigned char> bytes(28 + constants.size() * 20, 0);
	auto dword = [&](std::size_t at, std::uint32_t value) { std::memcpy(bytes.data() + at, &value, 4); };
	auto word = [&](std::size_t at, std::uint32_t value) { std::memcpy(bytes.data() + at, &value, 2); };
	dword(0, 28);
	dword(12, static_cast<std::uint32_t>(constants.size()));
	dword(16, 28);
	for (std::size_t i = 0; i < constants.size(); ++i) {
		const std::size_t record = 28 + i * 20;
		dword(record, static_cast<std::uint32_t>(bytes.size()));
		word(record + 4, constants[i].second.first);
		word(record + 6, constants[i].second.second);
		word(record + 8, 1);
		bytes.insert(bytes.end(), constants[i].first.begin(), constants[i].first.end());
		bytes.push_back(0);
	}
	while (bytes.size() % 4 != 0) {
		bytes.push_back(0xAB);  // what lies behind a name is not the name
	}
	Tokens comment{0, kConstantTable};
	comment.resize(2 + bytes.size() / 4);
	std::memcpy(comment.data() + 2, bytes.data(), bytes.size());
	comment[0] = kComment | (static_cast<std::uint32_t>(comment.size() - 1) << 16);
	return comment;
}

Tokens Start(std::uint32_t version, const Tokens& table) {
	Tokens program{version};
	program.insert(program.end(), table.begin(), table.end());
	return program;
}

const Tokens kAmbientAt0 = Table({{"AmbientColor", {kFloatSet, 0}}, {"LightData", {kFloatSet, 9}}});

Found Find(const Tokens& program) {
	return FindLightSum(program.data(), program.size());
}

// The plain shape: mad r1.xyz, c0, r1.w, r2.w / add r1.xyz, r2, r1 / mul r0.xyz, r0, v0.
Tokens Plain(std::uint32_t addModifiers = kPartial) {
	Tokens program = Start(kPs2x, kAmbientAt0);
	Put(program, kMad, {Dst(kTemp, 1), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 2)});
	Put(program, kAdd, {Dst(kTemp, 1, kXyz, addModifiers), Src(kTemp, 2), Src(kTemp, 1)});
	Put(program, kMul, {Dst(kTemp, 0), Src(kTemp, 0), Src(kInput, 0)});
	program.push_back(kEnd);
	return program;
}

}  // namespace

int main() {
	Section("The key");
	Check(Shipped() == 1, "on as shipped");
	Check(On(1, true) && On(7, true), "on with the kinds");
	Check(!On(1, false) && !On(Shipped(), false), "without the kinds it does nothing: the game is not touched");
	Check(!On(0, true) && !On(-1, true), "0 and a negative value are off");

	Section("The bytecode's fields");
	Check(OpOf(0x03000002) == kAdd && LengthOf(0x03000002) == 3, "an add of three parameters");
	Check(OpOf(kComment | (5u << 16)) == kComment && CommentWords(kComment | (5u << 16)) == 5, "a comment of five words");
	Check(TypeOf(Src(kTemp, 3)) == kTemp && NumberOf(Src(kTemp, 3)) == 3, "r3");
	Check(TypeOf(Src(kConst, 21)) == kConst && NumberOf(Src(kConst, 21)) == 21, "c21");
	Check(TypeOf(Src(kSampler, 2)) == kSampler && TypeOf(Src(kInput, 0)) == kInput, "a type whose number needs the high bits, and v0");
	Check(WritesXyz(Dst(kTemp, 1)) && !WritesXyz(Dst(kTemp, 1, kW)) && WritesXyz(Dst(kTemp, 1, 15)), "xyz, w alone, all four");
	Check(IsPixelProgram(kPs2x) && IsPixelProgram(kPs30) && IsPixelProgram(0xFFFF0200), "pixel programs of version 2 and 3");
	Check(!IsPixelProgram(kVs20) && !IsPixelProgram(kPs14) && !IsPixelProgram(0), "no vertex program, no version 1, no nothing");
	Check(WithSaturate(Dst(kTemp, 1)) == (Dst(kTemp, 1) | kSaturate) && (WithSaturate(Dst(kTemp, 1)) & kPartial) != 0, "the saturate added, the rest kept");
	Check(TokensOf(1024) == 256 && TokensOf(8) == 2, "a length in bytes as tokens");
	Check(TokensOf(0) == 0 && TokensOf(1022) == 0 && TokensOf(kMostBytes + 4) == 0 && TokensOf(kMostBytes) == kMostBytes / 4, "no length a program has: none");

	Section("The constant table");
	{
		const Tokens program = Start(kPs2x, Table({{"MatAlpha", {kFloatSet, 3}}, {"AmbientColor", {kFloatSet, 1}}, {"BaseMap", {3, 0}}}));
		Check(AmbientRegister(program.data(), program.size()) == 1, "the register by the name, whichever record");
	}
	{
		const Tokens program = Start(kPs2x, Table({{"AmbientColour", {kFloatSet, 2}}, {"Ambient", {kFloatSet, 4}}, {"xAmbientColor", {kFloatSet, 5}}}));
		Check(AmbientRegister(program.data(), program.size()) == kNone, "a longer, a shorter and a prefixed name are not it");
	}
	{
		const Tokens program = Start(kPs2x, Table({{"AmbientColor", {3, 2}}}));
		Check(AmbientRegister(program.data(), program.size()) == kNone, "the name on a sampler is not the float constant");
	}
	{
		Tokens program{kPs2x, kComment | (2u << 16), 0x12345678, 0x9ABCDEF0};
		program.insert(program.end(), kAmbientAt0.begin(), kAmbientAt0.end());
		program.push_back(kEnd);
		Check(AmbientRegister(program.data(), program.size()) == 0, "another comment before the table is passed");
	}
	{
		Tokens program{kPs2x};
		Put(program, kMov, {Dst(kTemp, 0), Src(kConst, 0)});
		program.insert(program.end(), kAmbientAt0.begin(), kAmbientAt0.end());
		Check(AmbientRegister(program.data(), program.size()) == 0, "so is an instruction");
	}
	{
		const Tokens program{kPs2x, kEnd};
		Check(AmbientRegister(program.data(), program.size()) == kNone, "no table");
	}
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		program.resize(program.size() - 2);
		Check(AmbientRegister(program.data(), program.size()) == kNone, "a table that runs past the end");
	}
	{
		Tokens table = Table({{"AmbientColor", {kFloatSet, 0}}});
		table[2 + 4] = 4096;  // the records' offset
		Check(AmbientRegister(Start(kPs2x, table).data(), table.size() + 1) == kNone, "records beyond the table");
		table = Table({{"AmbientColor", {kFloatSet, 0}}});
		table[2 + 3] = 1000;  // the count of constants
		Check(AmbientRegister(Start(kPs2x, table).data(), table.size() + 1) == kNone, "more constants than the table holds");
		table = Table({{"AmbientColor", {kFloatSet, 0}}});
		table[2 + 7] = 4096;  // the first record's name offset
		Check(AmbientRegister(Start(kPs2x, table).data(), table.size() + 1) == kNone, "a name beyond the table");
		const Tokens tiny{kComment | (3u << 16), kConstantTable, 0, 0};
		Check(AmbientRegister(Start(kPs2x, tiny).data(), tiny.size() + 1) == kNone, "a table shorter than its header");
	}
	{
		// The name is the table's last bytes and has no end inside it.
		std::vector<unsigned char> bytes(28 + 20, 0);
		const std::uint32_t header[7] = {28, 0, 0, 1, 28, 0, 0};
		std::memcpy(bytes.data(), header, 28);
		const std::uint32_t nameAt = 48;
		std::memcpy(bytes.data() + 28, &nameAt, 4);
		const std::uint16_t set = kFloatSet;
		std::memcpy(bytes.data() + 32, &set, 2);
		const char name[12] = {'A', 'm', 'b', 'i', 'e', 'n', 't', 'C', 'o', 'l', 'o', 'r'};
		bytes.insert(bytes.end(), name, name + 12);
		Tokens comment{0, kConstantTable};
		comment.resize(2 + bytes.size() / 4);
		std::memcpy(comment.data() + 2, bytes.data(), bytes.size());
		comment[0] = kComment | (static_cast<std::uint32_t>(comment.size() - 1) << 16);
		const Tokens program = Start(kPs2x, comment);
		Check(AmbientRegister(program.data(), program.size()) == kNone, "a name without its end inside the table");
	}

	Section("The plain shape: ambient into r1, then add r1, r2, r1");
	{
		const Tokens program = Plain();
		const Found found = Find(program);
		const std::size_t add = 1 + kAmbientAt0.size() + 5;
		Check(found.outcome == Outcome::kClip, "found, to be clipped");
		Check(found.destination == add + 1 && OpOf(program[add]) == kAdd, "the token is the add's destination");
		Check(TypeOf(program[found.destination]) == kTemp && NumberOf(program[found.destination]) == 1, "which is r1");
	}
	Check(Find(Plain(kPartial | kSaturate)).outcome == Outcome::kAlready, "saturated already: nothing to do");
	Check(Find(Plain(0)).outcome == Outcome::kClip, "without partial precision too");
	{
		Tokens program = Plain();
		program[0] = kPs30;
		Check(Find(program).outcome == Outcome::kClip, "a version 3 program the same");
	}

	Section("The other shapes of the one-pass programs");
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		Put(program, kMad, {Dst(kTemp, 1), Src(kTemp, 3), Src(kTemp, 1), Src(kConst, 0)});  // glow x emit + ambient
		Put(program, kMad, {Dst(kTemp, 1), Src(kTemp, 1), Src(kConst, 5), Src(kTemp, 1)});  // reworked from itself
		const std::size_t add = program.size();
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 2), Src(kTemp, 1)});
		program.push_back(kEnd);
		const Found found = Find(program);
		Check(found.outcome == Outcome::kClip && found.destination == add + 1, "the glow variant: the rework in between is passed");
	}
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		Put(program, kMad, {Dst(kTemp, 3), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 2)});
		const std::size_t add = program.size();
		Put(program, kAdd, {Dst(kTemp, 3), Src(kTemp, 4), Src(kTemp, 3)});
		program.push_back(kEnd);
		const Found found = Find(program);
		Check(found.outcome == Outcome::kClip && found.destination == add + 1 && NumberOf(program[found.destination]) == 3, "the FaceGen and hair variants: another temporary");
	}
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		Put(program, kMad, {Dst(kTemp, 2), Src(kConst, 0), Src(kTemp, 4), Src(kTemp, 6)});
		Put(program, kAdd, {Dst(kTemp, 2), Src(kTemp, 2), Src(kTemp, 6)});
		program.push_back(kEnd);
		Check(Find(program).outcome == Outcome::kClip, "the add's sources the other way round");
	}
	{
		Tokens program = Start(kPs2x, Table({{"AmbientColor", {kFloatSet, 1}}}));
		Put(program, kMov, {Dst(kTemp, 5), Src(kConst, 0)});  // c0 is something else here
		Put(program, kMad, {Dst(kTemp, 1), Src(kConst, 1), Src(kTemp, 1), Src(kTemp, 2)});
		const std::size_t add = program.size();
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 2), Src(kTemp, 1)});
		program.push_back(kEnd);
		const Found found = Find(program);
		Check(found.outcome == Outcome::kClip && found.destination == add + 1, "the ambient colour in another register: the table says which");
	}
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		Put(program, kMad, {Dst(kTemp, 1), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 2)});
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 1), Src(kConst, 2)});  // a constant added: a rework
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 1), Src(kTemp, 1)});   // doubled: a rework
		Put(program, kAdd, {Dst(kTemp, 4), Src(kTemp, 2), Src(kTemp, 1)});   // another temporary's sum
		Put(program, kAdd, {Dst(kTemp, 1, kW), Src(kTemp, 2), Src(kTemp, 1)});  // its w alone
		const std::size_t add = program.size();
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 2), Src(kTemp, 1)});
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 3), Src(kTemp, 1)});  // a later one is not it
		program.push_back(kEnd);
		const Found found = Find(program);
		Check(found.outcome == Outcome::kClip && found.destination == add + 1, "adds that are not the sum are passed; the first that is, is taken");
	}
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		Put(program, kMov, {Dst(kTemp, 1, kW), Src(kConst, 0)});  // the ambient colour's w into a scalar
		Put(program, kMov, {Dst(8, 0), Src(kConst, 0)});          // straight to the output
		Put(program, kDef, {Dst(kConst, 7), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 2), Src(kTemp, 1)});  // a definition whose numbers look like registers
		Put(program, kMad, {Dst(kTemp, 2), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 3)});
		const std::size_t add = program.size();
		Put(program, kAdd, {Dst(kTemp, 2), Src(kTemp, 3), Src(kTemp, 2)});
		program.push_back(kEnd);
		const Found found = Find(program);
		Check(found.outcome == Outcome::kClip && found.destination == add + 1, "only a read into a temporary's xyz starts the sum");
	}

	Section("What is left alone");
	{
		Tokens program = Plain();
		program[0] = kVs20;
		Check(Find(program).outcome == Outcome::kNoProgram, "a vertex program");
		program[0] = kPs14;
		Check(Find(program).outcome == Outcome::kNoProgram, "a version 1 pixel program");
	}
	{
		Tokens program = Start(kPs2x, Table({{"MatAlpha", {kFloatSet, 3}}, {"NormalMap", {3, 0}}}));
		Put(program, kMad, {Dst(kTemp, 1), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 2)});
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 2), Src(kTemp, 1)});
		program.push_back(kEnd);
		Check(Find(program).outcome == Outcome::kNoAmbient, "a program without the constant: no lighting program, the same instructions or not");
	}
	{
		Tokens program{kPs2x};
		Put(program, kMad, {Dst(kTemp, 1), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 2)});
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 2), Src(kTemp, 1)});
		program.push_back(kEnd);
		Check(Find(program).outcome == Outcome::kNoAmbient, "a program without a table");
	}
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		Put(program, kMad, {Dst(kTemp, 1), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 2)});
		Put(program, kMov, {Dst(kTemp, 1), Src(kConst, 4)});  // r1 is something else from here
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 2), Src(kTemp, 1)});
		program.push_back(kEnd);
		Check(Find(program).outcome == Outcome::kNoSum, "the temporary written anew before a sum: not the shape, nothing touched");
	}
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		Put(program, kMad, {Dst(kTemp, 1), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 2)});
		Put(program, kMul, {Dst(kTemp, 0), Src(kTemp, 0), Src(kTemp, 1)});
		program.push_back(kEnd);
		Check(Find(program).outcome == Outcome::kNoSum, "the ambient colour used and never summed");
	}
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		Put(program, kMov, {Dst(kTemp, 1), Src(kConst, 3)});
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 2), Src(kTemp, 1)});
		program.push_back(kEnd);
		Check(Find(program).outcome == Outcome::kNoSum, "the ambient colour never read");
	}
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		Put(program, kMad, {Dst(kTemp, 1), Src(kConst, 0) | kRelative, Src(kTemp, 1), Src(kTemp, 2)});
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 2), Src(kTemp, 1)});
		program.push_back(kEnd);
		Check(Find(program).outcome == Outcome::kNoSum, "a read through another register is not read as the constant");
	}
	{
		Tokens program = Start(kPs2x, kAmbientAt0);
		Put(program, kMad, {Dst(kTemp, 1), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 2)});
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 2) | kRelative, Src(kTemp, 1)});
		Put(program, kAdd, {Dst(kTemp, 1), Src(kTemp, 2), Src(kTemp, 1)});
		program.push_back(kEnd);
		Check(Find(program).outcome == Outcome::kNoSum, "a write to the sum's temporary in such a way: not the shape, the add after it is not taken");
	}

	Section("Streams that do not hold together");
	{
		const Tokens one{kPs2x};
		Check(FindLightSum(one.data(), one.size()).outcome == Outcome::kBroken && FindLightSum(one.data(), 0).outcome == Outcome::kBroken, "one token or none");
		Tokens program = Plain();
		program.pop_back();
		Check(Find(program).outcome == Outcome::kClip, "found before the missing end: the add is whole");
		program = Start(kPs2x, kAmbientAt0);
		Put(program, kMad, {Dst(kTemp, 1), Src(kConst, 0), Src(kTemp, 1), Src(kTemp, 2)});
		Check(Find(program).outcome == Outcome::kBroken, "no end and no sum");
		program.push_back(kAdd | (3u << 24));
		program.push_back(Dst(kTemp, 1));
		Check(Find(program).outcome == Outcome::kBroken, "an instruction that runs past the end");
	}

	Section("The counts");
	{
		Counts counts;
		Count(counts, true, Outcome::kClip, true);
		Count(counts, true, Outcome::kClip, true);
		Count(counts, true, Outcome::kClip, false);
		Count(counts, true, Outcome::kAlready, false);
		Count(counts, true, Outcome::kNoProgram, false);
		Count(counts, true, Outcome::kNoAmbient, false);
		Count(counts, true, Outcome::kNoSum, false);
		Count(counts, true, Outcome::kBroken, false);
		Count(counts, false, Outcome::kClip, true);
		Check(counts.asked == 9, "every program asked for");
		Check(counts.clipped == 2 && counts.notWritten == 1, "clipped, and found without the write going through");
		Check(counts.already == 1 && counts.noProgram == 1 && counts.noAmbient == 1 && counts.noSum == 1 && counts.broken == 1, "one of each other outcome");
		Check(counts.noEntry == 1, "a name the package does not hold counts as that alone");
	}

	Section("The registers a constant takes");
	{
		// The table's records are 20 bytes from byte 28 on; the count of registers is the word at +8 of a record -
		// the low word of the comment's token 11 + 5 x the record's number.
		auto withRegisters = [](Tokens table, std::size_t record, std::uint32_t registers) {
			table[11 + 5 * record] = (table[11 + 5 * record] & 0xFFFF0000u) | registers;
			return table;
		};
		const Tokens seven = Start(kPs2x, withRegisters(kAmbientAt0, 1, 14));
		const Tokens eight = Start(kPs2x, withRegisters(kAmbientAt0, 1, 16));
		Check(ConstantRegisters(seven.data(), seven.size(), "LightData") == 14, "LightData x14: seven lights, two registers each");
		Check(ConstantRegisters(eight.data(), eight.size(), "LightData") == 16, "LightData x16: eight");
		Check(ConstantRegisters(seven.data(), seven.size(), "AmbientColor") == 1, "another constant of the same table keeps its own count");
		Check(ConstantRegisters(seven.data(), seven.size(), "LightDat") == 0 && ConstantRegisters(seven.data(), seven.size(), "LightData2") == 0,
		      "a name that only begins so, or goes on: none");
		const Tokens sampler = Start(kPs2x, withRegisters(Table({{"LightData", {3, 9}}}), 0, 14));
		Check(ConstantRegisters(sampler.data(), sampler.size(), "LightData") == 0, "the name on a sampler is no float constant");
		const Tokens bare{kPs2x, kEnd};
		Check(ConstantRegisters(bare.data(), bare.size(), "LightData") == 0, "a program without a table");
		Tokens cut = seven;
		cut.resize(6);
		Check(ConstantRegisters(cut.data(), cut.size(), "LightData") == 0, "a table that runs past the end of the stream");
		Tokens wrong = withRegisters(kAmbientAt0, 1, 14);
		wrong[2 + 4] = 0x00100000;  // the records' offset beyond the table
		Check(ConstantRegisters(Start(kPs2x, wrong).data(), wrong.size() + 1, "LightData") == 0, "records beyond the table");
		const TablePlace place = TableOf(seven.data(), seven.size());
		Check(place.at == 3 && place.bytes == (kAmbientAt0.size() - 2) * 4, "the table's place: behind the version, the comment's token and 'CTAB'");
		Check(TableOf(bare.data(), bare.size()).bytes == 0, "no table: no bytes");
	}
	return Failures();
}
