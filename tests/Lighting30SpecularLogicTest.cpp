// Every flow of the one-pass lighting's specular fade (src/engine/render/Lighting30SpecularLogic.h): the key, the
// chain's fade by distance, the distance to a bound, the toggle a program gets, the sampler by its name, the gloss
// multiply in the shapes the programs have and everything it must refuse, the program with the multiply put in,
// the counts. The programs are built token by token, as in Lighting30ClipLogicTest.cpp.

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "Check.h"
#include "engine/render/Lighting30SpecularLogic.h"

using namespace obng::engine::render::lighting30_specular;
using namespace obng::test;

namespace {

using Tokens = std::vector<std::uint32_t>;
namespace c = obng::engine::render::lighting30_clip;

constexpr std::uint32_t kPs2x = 0xFFFF0201, kVs20 = 0xFFFE0200;
constexpr std::uint32_t kAddOp = 2, kMadOp = 4, kDp3 = 8;
constexpr std::uint32_t kInput = 1, kTexcoord = 3;
constexpr std::uint32_t kXyz = 7, kW = 8, kAll = 15;
constexpr std::uint32_t kSwizzleW = 0xFF, kIdentity = 0xE4;

std::uint32_t TypeBits(std::uint32_t type) {
	return ((type & 7) << 28) | ((type & 0x18) << 8);
}

std::uint32_t Dst(std::uint32_t type, std::uint32_t number, std::uint32_t mask = kXyz) {
	return 0x80000000u | TypeBits(type) | number | (mask << 16) | kPartial;
}

std::uint32_t Src(std::uint32_t type, std::uint32_t number, std::uint32_t swizzle = kIdentity) {
	return 0x80000000u | TypeBits(type) | number | (swizzle << 16);
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
		bytes.push_back(0xAB);
	}
	Tokens comment{0, c::kConstantTable};
	comment.resize(2 + bytes.size() / 4);
	std::memcpy(comment.data() + 2, bytes.data(), bytes.size());
	comment[0] = c::kComment | (static_cast<std::uint32_t>(comment.size() - 1) << 16);
	return comment;
}

const Tokens kTable = Table({{"AmbientColor", {c::kFloatSet, 0}}, {"ToggleADTS", {c::kFloatSet, 5}}, {"BaseMap", {kSamplerSet, 0}}, {"NormalMap", {kSamplerSet, 1}}});

Tokens Start(const Tokens& table = kTable, std::uint32_t version = kPs2x) {
	Tokens program{version};
	program.insert(program.end(), table.begin(), table.end());
	return program;
}

void Sample(Tokens& p, std::uint32_t temp, std::uint32_t sampler) {
	Put(p, kTexld, {Dst(c::kTemp, temp, kAll), Src(kTexcoord, 0), Src(kSampler, sampler)});
}

void End(Tokens& p) {
	p.push_back(c::kEnd);
}

// The shape of SM3001.pso's ending: the normal map into r1, the specular summed in r2, times the gloss, added.
Tokens Specular(bool glossFirst = true) {
	Tokens p = Start();
	Sample(p, 1, 1);
	Sample(p, 0, 0);
	Put(p, kDp3, {Dst(c::kTemp, 5, 1), Src(c::kTemp, 1), Src(kInput, 0)});
	Put(p, kPow, {Dst(c::kTemp, 4, kW), Src(c::kTemp, 5, 0x00), Src(c::kConst, 6, 0xAA)});
	Put(p, kAddOp, {Dst(c::kTemp, 2), Src(c::kTemp, 2), Src(c::kTemp, 3)});
	if (glossFirst) {
		Put(p, kMul, {Dst(c::kTemp, 2), Src(c::kTemp, 1, kSwizzleW), Src(c::kTemp, 2)});
	} else {
		Put(p, kMul, {Dst(c::kTemp, 2), Src(c::kTemp, 2), Src(c::kTemp, 1, kSwizzleW)});
	}
	Put(p, kMadOp, {Dst(c::kTemp, 1), Src(c::kTemp, 1), Src(c::kTemp, 0), Src(c::kTemp, 2)});
	End(p);
	return p;
}

Found Find(const Tokens& p) {
	return FindGlossMultiply(p.data(), p.size());
}

// The index of the token behind the instruction that starts at `op` in a program built here.
std::size_t Behind(const Tokens& p, std::uint32_t op, std::size_t nth = 0) {
	for (std::size_t i = 1 + kTable.size(); i < p.size(); i += 1 + c::LengthOf(p[i])) {
		if (c::OpOf(p[i]) == op && nth-- == 0) {
			return i + 1 + c::LengthOf(p[i]);
		}
	}
	return 0;
}

}  // namespace

int main() {
	Section("The key");
	Check(Shipped() == 1 && On(Shipped(), true), "on as shipped, with the kinds");
	Check(!On(1, false) && !On(0, true) && !On(-1, true), "off without the kinds, at 0, below");

	Section("The chain's fade by distance");
	Check(Fade(100.0f, 500.0f, 800.0f) == 1.0f && Fade(-50.0f, 500.0f, 800.0f) == 1.0f, "nearer than the start: full, also inside the bound");
	Check(Fade(500.0f, 500.0f, 800.0f) == 1.0f, "at the start: still full");
	Check(Fade(650.0f, 500.0f, 800.0f) == 0.5f && Fade(575.0f, 500.0f, 800.0f) == 0.75f, "linear between");
	Check(Fade(800.0f, 500.0f, 800.0f) == 0.0f && Fade(5000.0f, 500.0f, 800.0f) == 0.0f, "at the end and beyond: none");
	Check(Fade(5000.0f, 500.0f, 0.0f) == 1.0f && Fade(5000.0f, 500.0f, -1.0f) == 1.0f, "an end of 0 or less fades nothing, as the chain computes none");
	Check(Fade(600.0f, 600.0f, 600.0f) == 0.0f && Fade(599.0f, 600.0f, 600.0f) == 1.0f, "start and end the same: a step there");
	Check(Fade(700.0f, 800.0f, 600.0f) == 1.0f && Fade(900.0f, 800.0f, 600.0f) == 0.0f, "an end before the start: full before the start, none beyond the end");

	Section("The distance to a bound");
	{
		const float camera[3] = {0.0f, 0.0f, 0.0f};
		const float center[3] = {300.0f, 400.0f, 0.0f};
		Check(DistanceToBound(camera, center, 100.0f) == 400.0f, "the centre's distance less the radius");
		Check(DistanceToBound(camera, center, 600.0f) == -100.0f, "inside the bound: below 0");
	}

	Section("The toggle a program gets");
	Check(ToggleFor(1.0f, 0.25f) == 0.25f && ToggleFor(1.0f, 1.0f) == 1.0f && ToggleFor(1.0f, 0.0f) == 0.0f, "a specular program's toggle: the fade");
	Check(ToggleFor(0.0f, 0.25f) == 0.0f, "no specular: left at 0");

	Section("Which setups are faded");
	Check(FadesPass(0x12D) && FadesPass(0x135) && FadesPass(0x13A) && FadesPass(0x146), "the lighting records, with and without the projected shadow");
	Check(!FadesPass(0x12C) && !FadesPass(0x147) && !FadesPass(0x14E) && !FadesPass(0x151) && !FadesPass(0x154) && !FadesPass(4), "the actor shadow's, the environment map's, the depth map's, the z pass");

	Section("A sampler by its name");
	{
		const Tokens p = Specular();
		Check(SamplerRegister(p.data(), p.size(), "NormalMap") == 1 && SamplerRegister(p.data(), p.size(), "BaseMap") == 0, "the two samplers");
		Check(SamplerRegister(p.data(), p.size(), "GlowMap") == c::kNone, "a sampler the program does not have");
		Check(SamplerRegister(p.data(), p.size(), "AmbientColor") == c::kNone, "a float constant is no sampler");
		const Tokens bare{kPs2x, c::kEnd};
		Check(SamplerRegister(bare.data(), bare.size(), "NormalMap") == c::kNone, "no table");
	}

	Section("The gloss multiply in the programs' shape");
	{
		const Tokens p = Specular();
		const Found f = Find(p);
		Check(f.outcome == Outcome::kFade && f.specular == 2 && f.toggle == 5, "found: the specular in r2, the toggle c5");
		Check(f.after == Behind(p, kMul), "the place behind the multiply");
		const Found g = Find(Specular(false));
		Check(g.outcome == Outcome::kFade && g.specular == 2, "the sources the other way round");
	}

	Section("What is not the gloss multiply");
	{
		Tokens p = Start();
		Sample(p, 1, 1);
		Put(p, kMadOp, {Dst(c::kTemp, 1), Src(c::kTemp, 1), Src(c::kTemp, 0), Src(c::kTemp, 2)});
		End(p);
		Check(Find(p).outcome == Outcome::kNoSpecular, "no multiply: a program without specular");
	}
	{
		Tokens p = Start();
		Put(p, kMul, {Dst(c::kTemp, 2), Src(c::kTemp, 1, kSwizzleW), Src(c::kTemp, 2)});
		Sample(p, 1, 1);
		End(p);
		Check(Find(p).outcome == Outcome::kNoSpecular, "a multiply before the normal map is sampled");
	}
	{
		Tokens p = Start();
		Sample(p, 1, 0);
		Put(p, kMul, {Dst(c::kTemp, 2), Src(c::kTemp, 1, kSwizzleW), Src(c::kTemp, 2)});
		End(p);
		Check(Find(p).outcome == Outcome::kNoSpecular, "the alpha of the base map, not the normal map");
	}
	{
		Tokens p = Start();
		Sample(p, 1, 1);
		Put(p, kMul, {Dst(c::kTemp, 2), Src(c::kTemp, 1, 0xAA), Src(c::kTemp, 2)});
		Put(p, kMul, {Dst(c::kTemp, 2, kW), Src(c::kTemp, 1, kSwizzleW), Src(c::kTemp, 2)});
		Put(p, kMul, {Dst(c::kTemp, 2), Src(c::kTemp, 1, kSwizzleW), Src(c::kTemp, 3)});
		Put(p, kMul, {Dst(c::kTemp, 1), Src(c::kTemp, 1, kSwizzleW), Src(c::kTemp, 1)});
		Put(p, kMul, {Dst(c::kTemp, 2), Src(c::kTemp, 1, kSwizzleW), Src(c::kConst, 2)});
		End(p);
		Check(Find(p).outcome == Outcome::kNoSpecular, "another component, another mask, another temporary, the gloss's own register, a constant");
	}
	{
		Tokens p = Start();
		Sample(p, 1, 1);
		Put(p, kPow, {Dst(c::kTemp, 4, kW), Src(c::kTemp, 5, 0x00), Src(c::kConst, 6, 0xAA)});
		Put(p, kMul, {Dst(c::kTemp, 2), Src(c::kTemp, 1, kSwizzleW) | c::kRelative, Src(c::kTemp, 2)});
		End(p);
		Check(Find(p).outcome == Outcome::kNoSpecular, "a source addressed through another register");
	}
	{
		Tokens p = Start();
		Sample(p, 1, 1);
		Put(p, kPow, {Dst(c::kTemp, 4, kW), Src(c::kTemp, 5, 0x00), Src(c::kConst, 6, 0xAA)});
		Put(p, kMul, {Dst(c::kTemp, 4) | c::kSaturate, Src(c::kTemp, 1, kSwizzleW), Src(c::kTemp, 4)});
		End(p);
		Check(Find(p).outcome == Outcome::kNoSpecular, "a saturated multiply (SM3016's, FaceGen with the projected shadow): not specular");
	}
	{
		Tokens p = Start();
		Sample(p, 1, 1);
		Put(p, kMul, {Dst(c::kTemp, 2), Src(c::kTemp, 1, kSwizzleW), Src(c::kTemp, 2)});
		End(p);
		Check(Find(p).outcome == Outcome::kNoSpecular, "the shape without a pow anywhere: a program without specular");
	}
	{
		Tokens p = Start();
		Sample(p, 1, 1);
		Put(p, kMul, {Dst(c::kTemp, 2), Src(c::kTemp, 1, kSwizzleW), Src(c::kTemp, 2)});
		Put(p, kMul, {Dst(c::kTemp, 3), Src(c::kTemp, 1, kSwizzleW), Src(c::kTemp, 3)});
		End(p);
		Check(Find(p).outcome == Outcome::kBroken, "two of them: not the shape read");
	}

	Section("Programs it leaves alone");
	{
		Tokens p = Specular();
		p[0] = kVs20;
		Check(Find(p).outcome == Outcome::kBroken, "a vertex program");
		const Tokens noNormal = Start(Table({{"ToggleADTS", {c::kFloatSet, 5}}, {"BaseMap", {kSamplerSet, 0}}}));
		Check(Find(noNormal).outcome == Outcome::kNoTable, "no normal map in the table");
		Tokens noToggle = Start(Table({{"NormalMap", {kSamplerSet, 1}}}));
		Sample(noToggle, 1, 1);
		End(noToggle);
		Check(Find(noToggle).outcome == Outcome::kNoTable, "no ToggleADTS in the table");
		const Tokens bare{kPs2x, c::kEnd};
		Check(Find(bare).outcome == Outcome::kNoTable, "no table at all");
		Tokens cut = Specular();
		cut.resize(cut.size() - 3);
		Check(Find(cut).outcome == Outcome::kBroken, "an instruction that runs past the end");
		Tokens open = Specular();
		open.pop_back();
		Check(Find(open).outcome == Outcome::kBroken, "no end token");
		Check(FindGlossMultiply(open.data(), 1).outcome == Outcome::kBroken, "a stream of one token");
	}

	Section("A program's slot by its name");
	Check(SlotOf("SM3000.pso") == 0 && SlotOf("SM3001.pso") == 1 && SlotOf("SM3019.pso") == 19, "the lighting programs: 0 .. 19");
	Check(SlotOf("SM3LL000.pso") == 20 && SlotOf("SM3LL001.pso") == 21 && SlotOf("SM3LL019.pso") == 39, "the four-light ones: 20 .. 39");
	Check(SlotOf("SM3020.pso") == kNoSlot && SlotOf("SM3LL020.pso") == kNoSlot, "the shader's other programs");
	Check(SlotOf("SM3001.vso") == kNoSlot && SlotOf("SM3LL01.pso") == kNoSlot && SlotOf("SM3LLx01.pso") == kNoSlot && SlotOf("SM3001.psox") == kNoSlot,
	      "a vertex program, two digits, a letter, another ending");
	Check(SlotOf(nullptr) == kNoSlot && SlotOf("") == kNoSlot && SlotOf("SLS2003.pso") == kNoSlot, "no name, an empty one, another family");

	Section("The program with the multiply put in");
	{
		const Tokens p = Specular();
		const Found f = Find(p);
		std::vector<std::uint32_t> out(p.size() + kAdded + 8, 0xDEADBEEF);
		const std::size_t n = WithFade(p.data(), p.size(), out.data(), out.size());
		Check(n == p.size() + kAdded, "four tokens more");
		bool before = true;
		for (std::size_t i = 0; i < f.after; ++i) {
			before = before && out[i] == p[i];
		}
		Check(before, "everything up to the gloss multiply as it was");
		std::uint32_t added[kAdded] = {};
		FadeMultiply(2, 5, added);
		Check(out[f.after] == added[0] && out[f.after + 1] == added[1] && out[f.after + 2] == added[2] && out[f.after + 3] == added[3], "then the multiply");
		bool after = true;
		for (std::size_t i = f.after; i < p.size(); ++i) {
			after = after && out[i + kAdded] == p[i];
		}
		Check(after && out[n - 1] == c::kEnd, "then the rest, the end token last");
		Check(c::OpOf(added[0]) == kMul && c::LengthOf(added[0]) == 3, "a mul of three parameters");
		Check(c::TypeOf(added[1]) == c::kTemp && c::NumberOf(added[1]) == 2 && MaskOf(added[1]) == 7 && (added[1] & kPartial) != 0, "into r2.xyz at partial precision");
		Check(c::TypeOf(added[2]) == c::kTemp && c::NumberOf(added[2]) == 2 && SwizzleOf(added[2]) == kIdentity, "from r2");
		Check(c::TypeOf(added[3]) == c::kConst && c::NumberOf(added[3]) == 5 && SwizzleOf(added[3]) == kSwizzleW, "times c5.w");
		Check(Find(Tokens(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n))).outcome == Outcome::kFade, "the result still reads as a specular program");
	}
	{
		const Tokens p = Specular();
		std::vector<std::uint32_t> small(p.size() + kAdded - 1);
		Check(WithFade(p.data(), p.size(), small.data(), small.size()) == 0, "too little room: nothing written");
		Tokens plain = Start();
		Sample(plain, 1, 1);
		End(plain);
		std::vector<std::uint32_t> out(plain.size() + kAdded);
		Check(WithFade(plain.data(), plain.size(), out.data(), out.size()) == 0, "a program without specular: none");
	}

	Section("The counts");
	{
		Counts counts;
		CountProgram(counts, Outcome::kFade);
		CountProgram(counts, Outcome::kFade);
		CountProgram(counts, Outcome::kNoSpecular);
		CountProgram(counts, Outcome::kNoTable);
		CountProgram(counts, Outcome::kBroken);
		Check(counts.programs == 5 && counts.faded == 2 && counts.noSpecular == 1 && counts.noTable == 1 && counts.broken == 1, "programs by outcome");
		CountSetup(counts, 1.0f);
		CountSetup(counts, 0.4f);
		CountSetup(counts, 0.0f);
		Check(counts.setups == 3 && counts.dimmed == 2 && counts.dark == 1, "setups, faded ones, dark ones");
	}
	return Failures();
}
