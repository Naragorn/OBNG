// Every flow of the rule that hands a moved geometry's pass list to the game's chain when the one pass cannot draw
// it (src/engine/render/Lighting30ListLogic.h): the key's values, how many lights a one-pass record holds, which
// pixel program a record gets and how many lights that draws, the most a list may hold, when a built record
// overflows, when a geometry's glow is the bloom's, which builder a geometry gets, which shader it carries, when a
// call of the getter decides anew, what it does then, the counts.

#include "Check.h"
#include "engine/render/Lighting30ListLogic.h"

using namespace obng::engine::render::lighting30_list;
using namespace obng::test;

namespace {

constexpr std::uint32_t kOnePassShader = 0x1A000000, kChainShader = 0x01000000, kSomeOtherShader = 0x0E000000;
constexpr std::uint32_t kValidKey = 0x52F;  // a render mode and a pass mask, as a getter leaves it

constexpr bool Same(Step step, Builder builder, bool change) {
	return step.builder == builder && step.change == change;
}

}  // namespace

int main() {
	Section("The key's values");
	Check(Shipped() == 8, "as shipped: what the record's own program draws, at most the eight the setup hands over");
	Check(MostOf(0) == 0 && MostOf(3) == 3 && MostOf(8) == 8, "a value is taken as it is");
	Check(MostOf(-1) == 0 && MostOf(-1000) == 0, "a negative value is 0: everything through the chain");
	Check(MostOf(255) == 255 && MostOf(256) == 255 && MostOf(100000) == 255, "capped at the value that means no limit");

	Section("The lights of a one-pass record");
	Check(RecordLights(0) == 1, "the scene's first light is always in it");
	Check(RecordLights(6) == 7 && RecordLights(7) == 8, "and every light of the property that is no actor's shadow");

	Section("The pixel program of a lighting record");
	Check(ProgramOf(0x12D, false) == 0 && ProgramOf(0x12D, true) == 1, "plain: SM3000, with specular SM3001");
	Check(ProgramOf(0x12E, false) == 0 && ProgramOf(0x12E, true) == 1, "skinned: the same pixel programs");
	Check(ProgramOf(0x135, false) == 0 && ProgramOf(0x136, true) == 1, "with vertex colours, plain and skinned: the same again");
	Check(ProgramOf(0x12F, false) == 2 && ProgramOf(0x12F, true) == 3, "hair");
	Check(ProgramOf(0x130, false) == 4 && ProgramOf(0x130, true) == 5 && ProgramOf(0x137, false) == 4 && ProgramOf(0x137, true) == 5, "parallax, with and without vertex colours");
	Check(ProgramOf(0x131, false) == 6 && ProgramOf(0x132, true) == 7, "FaceGen, plain and skinned");
	Check(ProgramOf(0x133, false) == 8 && ProgramOf(0x134, true) == 9, "glow, plain and skinned");
	Check(ProgramOf(0x138, false) == 8 && ProgramOf(0x139, true) == 9, "glow with vertex colours");
	Check(ProgramOf(0x13A, false) == 10 && ProgramOf(0x13B, true) == 11, "with the projected shadow: ten more");
	Check(ProgramOf(0x13C, false) == 12 && ProgramOf(0x13C, true) == 13, "hair with it");
	Check(ProgramOf(0x13D, false) == 14 && ProgramOf(0x144, true) == 15, "parallax with it");
	Check(ProgramOf(0x13E, false) == 16 && ProgramOf(0x13F, true) == 17, "FaceGen with it");
	Check(ProgramOf(0x140, false) == 18 && ProgramOf(0x141, true) == 19 && ProgramOf(0x145, false) == 18 && ProgramOf(0x146, true) == 19, "glow with it");
	Check(ProgramOf(0x142, false) == 10 && ProgramOf(0x143, true) == 11, "vertex colours with it");
	Check(ProgramOf(0x12C, true) == kNoProgram && ProgramOf(0x147, false) == kNoProgram && ProgramOf(0x14E, false) == kNoProgram && ProgramOf(4, true) == kNoProgram,
	      "no lighting record: no program");
	{
		bool inRange = true;
		for (std::uint16_t pass = 0x12D; pass <= 0x146; ++pass) {
			inRange = inRange && ProgramOf(pass, false) < kPrograms && ProgramOf(pass, true) < kPrograms && ProgramOf(pass, true) == ProgramOf(pass, false) + 1;
		}
		Check(inRange, "every lighting pass id has a program of the twenty, the one with specular the next number");
	}

	Section("A program's number by its name");
	Check(ProgramNumberOf("SM3000.pso") == 0 && ProgramNumberOf("SM3001.pso") == 1 && ProgramNumberOf("SM3019.pso") == 19, "the twenty lighting programs");
	Check(ProgramNumberOf("SM3020.pso") == kNoProgram && ProgramNumberOf("SM3038.pso") == kNoProgram, "the shader's other programs: not lighting ones");
	Check(ProgramNumberOf("SM3LL000.pso") == kNoProgram, "the four-light programs are not counted: they leave nothing out");
	Check(ProgramNumberOf("SM3000.vso") == kNoProgram && ProgramNumberOf("SM3000.ps") == kNoProgram && ProgramNumberOf("SM3000.psox") == kNoProgram,
	      "a vertex program, or another ending");
	Check(ProgramNumberOf("SM300.pso") == kNoProgram && ProgramNumberOf("SM30a0.pso") == kNoProgram && ProgramNumberOf("SLS2003.pso") == kNoProgram,
	      "two digits, a letter among them, another family");
	Check(ProgramNumberOf(nullptr) == kNoProgram && ProgramNumberOf("") == kNoProgram && ProgramNumberOf("SM") == kNoProgram, "no name, an empty one, a short one");

	Section("The lights a record is drawn with");
	Check(DrawnWith(8, 14) == 7, "LightData x14: seven");
	Check(DrawnWith(8, 16) == 8, "LightData x16: eight");
	Check(DrawnWith(8, 20) == 8, "a program built for ten: the setup hands it eight");
	Check(DrawnWith(8, 8) == 4, "a four-light program");
	Check(DrawnWith(8, 0) == kLightsDrawn && DrawnWith(8, 1) == kLightsDrawn, "a table that gave no count: the least any program draws");
	Check(DrawnWith(6, 16) == 6 && DrawnWith(6, 0) == 6, "a lower cap of the setup holds for any program");

	Section("The most lights a one-pass list may hold");
	Check(LimitFor(8, 0, 8) == 8, "nothing known of the program yet: the setup's cap - the build will tell");
	Check(LimitFor(8, 7, 8) == 7 && LimitFor(8, 8, 8) == 8, "what the property's program was seen to draw");
	Check(LimitFor(5, 7, 8) == 5 && LimitFor(5, 0, 8) == 5, "a lower key holds whatever the program draws");
	Check(LimitFor(0, 8, 8) == 0, "the key at 0: none - everything through the chain");
	Check(LimitFor(255, 7, 8) == kNoLimit && LimitFor(255, 0, 8) == kNoLimit, "the key at 255: no limit (the comparison)");
	Check(LimitFor(200, 7, 8) == 7, "a key between is no more than the program draws");

	Section("A record that overflows its program");
	Check(!Overflows(7, 8, 7) && !Overflows(8, 8, 8), "as many lights as the program draws: none left out");
	Check(Overflows(8, 8, 7), "eight lights for a program of seven: the list goes to the chain after all");
	Check(Overflows(9, 8, 8), "nine for a program of eight");
	Check(Overflows(6, 5, 7) && !Overflows(5, 5, 7), "a lower key is the limit");
	Check(Overflows(1, 0, 8), "the key at 0: any record");
	Check(!Overflows(40, 255, 7), "the key at 255: never");

	Section("Two colours the same");
	Check(SameColour(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f) && SameColour(0.25f, 0.5f, 1.0f, 0.25f, 0.5f, 1.0f), "every component equal");
	Check(!SameColour(0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f) && !SameColour(0.0f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f) && !SameColour(0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f),
	      "one component apart is enough, whichever");
	Check(SameColour(-0.0f, 0.0f, 0.0f, 0.0f, -0.0f, 0.0f), "a negative zero is zero, as the game's float compare has it");
	const float nan = __builtin_nanf("");
	Check(!SameColour(nan, 0.0f, 0.0f, nan, 0.0f, 0.0f) && !SameColour(0.0f, 0.0f, nan, 0.0f, 0.0f, 0.0f), "a NaN is the same as nothing: the game adds the pass then");

	Section("A glow the bloom is to take");
	Check(GlowsInBloom(true, 0x3F, true, false), "bloom on, the ambient pass on, a material with an emissive colour: the chain's alpha pass");
	Check(!GlowsInBloom(false, 0x3F, true, false), "no bloom: nothing reads the frame's alpha");
	Check(!GlowsInBloom(true, 0x3E, true, false) && !GlowsInBloom(true, 0, true, false), "the ambient pass off: the chain adds no such pass either");
	Check(!GlowsInBloom(true, 0x3F, false, false), "no material");
	Check(!GlowsInBloom(true, 0x3F, true, true), "a black emissive colour: nothing glows");
	Check(GlowsInBloom(true, 0x01, true, false), "the ambient bit alone is enough of the mask");

	Section("Which builder a geometry gets");
	Check(BuilderFor(1, 7, false) == Builder::kOnePass && BuilderFor(7, 7, false) == Builder::kOnePass, "up to the limit: the one pass");
	Check(BuilderFor(8, 7, false) == Builder::kChain && BuilderFor(12, 7, false) == Builder::kChain, "one more: the chain, which draws every light");
	Check(BuilderFor(8, 8, false) == Builder::kOnePass && BuilderFor(9, 8, false) == Builder::kChain, "a program of eight takes eight");
	Check(BuilderFor(1, 0, false) == Builder::kChain, "the limit 0: the first light alone is already too many");
	Check(BuilderFor(8, kNoLimit, false) == Builder::kOnePass && BuilderFor(255, kNoLimit, false) == Builder::kOnePass, "no limit: never for its lights");
	Check(BuilderFor(2, 7, true) == Builder::kChain, "a glow for the bloom: the chain, however few the lights");
	Check(BuilderFor(2, kNoLimit, true) == Builder::kChain, "and whatever the limit");
	Check(BuilderFor(9, 7, true) == Builder::kChain, "both reasons at once");

	Section("The shader a geometry carries");
	Check(CarriesFor(kOnePassShader, kOnePassShader, kChainShader) == Carries::kOnePass, "Lighting30Shader");
	Check(CarriesFor(kChainShader, kOnePassShader, kChainShader) == Carries::kChain, "ShadowLightShader");
	Check(CarriesFor(kSomeOtherShader, kOnePassShader, kChainShader) == Carries::kOther, "another shader: not this rule's");
	Check(CarriesFor(0, kOnePassShader, kChainShader) == Carries::kOther, "no shader");
	Check(CarriesFor(0, 0, 0) == Carries::kOther, "no shader, and the two not known yet: still none of the two");
	Check(CarriesFor(kOnePassShader, kOnePassShader, 0) == Carries::kOther, "the chain's shader not made yet: nothing to hand a geometry to, so not decided");
	Check(CarriesFor(kChainShader, 0, kChainShader) == Carries::kOther, "the one-pass shader not made yet: the same");

	Section("When a call decides anew");
	Check(Decides(true, Carries::kOnePass, 0) && Decides(true, Carries::kChain, 0), "a marked property with a stale list");
	Check(!Decides(true, Carries::kOnePass, kValidKey) && !Decides(true, Carries::kChain, kValidKey), "a valid list stays with its builder");
	Check(!Decides(false, Carries::kOnePass, 0) && !Decides(false, Carries::kChain, 0), "the game's own one-pass geometry is never decided on");
	Check(!Decides(true, Carries::kOther, 0), "a geometry that carries neither shader is left alone");

	Section("What a call does: the game's own one-pass geometry");
	Check(Same(StepFor(false, Carries::kOnePass, 0, 12, 7, true), Builder::kOnePass, false), "its own getter, whatever the lights and the glow");
	Check(Same(StepFor(false, Carries::kOnePass, kValidKey, 3, 7, false), Builder::kOnePass, false), "and with a valid list");
	Check(Same(StepFor(false, Carries::kChain, 0, 12, 7, false), Builder::kOnePass, false), "no mark: its own getter even under another shader");

	Section("What a call does: a moved geometry with a valid list");
	Check(Same(StepFor(true, Carries::kOnePass, kValidKey, 12, 7, true), Builder::kOnePass, false), "the one pass built it: the one-pass getter, nothing read");
	Check(Same(StepFor(true, Carries::kChain, kValidKey, 2, 7, false), Builder::kChain, false), "the chain built it: the chain's getter");

	Section("What a call does: a moved geometry with a stale list");
	Check(Same(StepFor(true, Carries::kOnePass, 0, 5, 7, false), Builder::kOnePass, false), "few lights, the one pass before: stays");
	Check(Same(StepFor(true, Carries::kOnePass, 0, 7, 7, false), Builder::kOnePass, false), "exactly the limit: stays");
	Check(Same(StepFor(true, Carries::kOnePass, 0, 8, 7, false), Builder::kChain, true), "one light more: handed to the chain");
	Check(Same(StepFor(true, Carries::kChain, 0, 9, 7, false), Builder::kChain, false), "still too many: stays with the chain");
	Check(Same(StepFor(true, Carries::kChain, 0, 7, 7, false), Builder::kOnePass, true), "a light gone: back to the one pass");
	Check(Same(StepFor(true, Carries::kOnePass, 0, 1, 0, false), Builder::kChain, true), "the limit 0: the first list already goes to the chain");
	Check(Same(StepFor(true, Carries::kOnePass, 0, 40, kNoLimit, false), Builder::kOnePass, false), "no limit: never for its lights");
	Check(Same(StepFor(true, Carries::kOnePass, 0, 2, 7, true), Builder::kChain, true), "a glow for the bloom: handed to the chain");
	Check(Same(StepFor(true, Carries::kChain, 0, 2, 7, true), Builder::kChain, false), "and kept there while it glows");
	Check(Same(StepFor(true, Carries::kChain, 0, 2, 7, false), Builder::kOnePass, true), "the glow gone (the bloom off, the colour black): back");

	Section("What a call does: a moved geometry under another shader");
	Check(Same(StepFor(true, Carries::kOther, 0, 12, 7, true), Builder::kOnePass, false), "left as it is: the property's own getter, nothing changed");
	Check(Same(StepFor(true, Carries::kOther, kValidKey, 12, 7, false), Builder::kOnePass, false), "and with a valid list");

	Section("The counts");
	Counts counts;
	Count(counts, false, Carries::kOnePass, 0, {9, 7, false}, StepFor(false, Carries::kOnePass, 0, 9, 7, false));
	Check(counts.decided == 0 && counts.other == 0, "the game's own geometry is not counted");
	Count(counts, true, Carries::kOnePass, kValidKey, {9, 7, false}, StepFor(true, Carries::kOnePass, kValidKey, 9, 7, false));
	Check(counts.decided == 0, "nor a call that found a valid list");
	Count(counts, true, Carries::kOnePass, 0, {4, 7, false}, StepFor(true, Carries::kOnePass, 0, 4, 7, false));
	Check(counts.decided == 1 && counts.chain == 0 && counts.toChain == 0 && counts.mostLights == 4, "a stale list decided for the one pass");
	Count(counts, true, Carries::kOnePass, 0, {8, 7, false}, StepFor(true, Carries::kOnePass, 0, 8, 7, false));
	Check(counts.decided == 2 && counts.chain == 1 && counts.manyLights == 1 && counts.toChain == 1 && counts.mostLights == 8, "one handed to the chain for its lights");
	Count(counts, true, Carries::kChain, 0, {10, 7, false}, StepFor(true, Carries::kChain, 0, 10, 7, false));
	Check(counts.decided == 3 && counts.chain == 2 && counts.manyLights == 2 && counts.toChain == 1 && counts.mostLights == 10, "one that stays there: a chain list, no change");
	Count(counts, true, Carries::kChain, 0, {6, 7, false}, StepFor(true, Carries::kChain, 0, 6, 7, false));
	Check(counts.decided == 4 && counts.chain == 2 && counts.toOnePass == 1 && counts.mostLights == 10, "and one back");
	Count(counts, true, Carries::kOnePass, 0, {3, 7, true}, StepFor(true, Carries::kOnePass, 0, 3, 7, true));
	Check(counts.decided == 5 && counts.chain == 3 && counts.glow == 1 && counts.manyLights == 2 && counts.toChain == 2, "one handed over for its glow alone");
	Count(counts, true, Carries::kChain, 0, {9, 7, true}, StepFor(true, Carries::kChain, 0, 9, 7, true));
	Check(counts.chain == 4 && counts.glow == 2 && counts.manyLights == 3, "a chain list for both reasons counts under both");
	Count(counts, true, Carries::kOther, 0, {6, 7, false}, StepFor(true, Carries::kOther, 0, 6, 7, false));
	Check(counts.other == 1 && counts.decided == 6, "a marked property under another shader: counted, not decided");
	Count(counts, true, Carries::kOnePass, 0, {40, kNoLimit, false}, StepFor(true, Carries::kOnePass, 0, 40, kNoLimit, false));
	Check(counts.decided == 7 && counts.chain == 4 && counts.manyLights == 3 && counts.mostLights == 40, "no limit: a one-pass list however many the lights");
	CountOverflow(counts);
	Check(counts.overflowed == 1 && counts.chain == 5 && counts.manyLights == 4 && counts.toChain == 3,
	      "a one-pass list whose record overflowed its program: a chain list for its lights, the geometry handed over");

	return Failures();
}
