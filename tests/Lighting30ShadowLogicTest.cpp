// Every flow of the actor shadow records for geometry moved to Lighting30Shader
// (src/engine/render/Lighting30ShadowLogic.h): when the game draws actor shadows at all, which lighting record a
// list has, which shadow pass goes with it, which properties are the probe's own, the counts.

#include "Check.h"
#include "engine/render/Lighting30ShadowLogic.h"

using namespace obng::engine::render::lighting30_shadow;
using namespace obng::test;

int main() {
	Section("The key's values");
	Check(Shipped() == 1 && On(Shipped()), "on as shipped: it only acts on geometry the kinds probe moved");
	Check(!On(0) && On(1) && On(2), "0 is off, anything else on");
	Check(!On(-1), "a negative value is off");

	Section("When the game draws actor shadows on receivers");
	Check(ShadowPassesOn(5, 0x3F), "render path 2_A with the mask's bit 0x10: as in the game");
	Check(ShadowPassesOn(3, 0x10) && ShadowPassesOn(7, 0x1F), "any path from 3 on");
	Check(!ShadowPassesOn(2, 0x3F) && !ShadowPassesOn(0, 0x3F), "not below path 3");
	Check(!ShadowPassesOn(5, 0x2F) && !ShadowPassesOn(5, 0), "not without the bit");

	Section("The lighting record's family");
	Check(IsLightingPass(0x12D) && IsLightingPass(0x146) && IsLightingPass(0x135), "0x12D .. 0x146");
	Check(!IsLightingPass(0x12C) && !IsLightingPass(0x147) && !IsLightingPass(0x14E) && !IsLightingPass(4), "nothing else");
	Check(WithoutProjectedShadow(0x13A) == 0x12D && WithoutProjectedShadow(0x146) == 0x139, "the variants with the projected shadow are 0x0D above");
	Check(WithoutProjectedShadow(0x12D) == 0x12D && WithoutProjectedShadow(0x139) == 0x139, "the plain ones stay");

	Section("The shadow pass that goes with a lighting record");
	Check(ShadowPassFor(0x12D) == 0x14E, "plain: 3XLIGHTING_SIMPLESHADOW");
	Check(ShadowPassFor(0x135) == 0x14F, "vertex colours: _Vc");
	Check(ShadowPassFor(0x12E) == 0x150, "skinned: _S");
	Check(ShadowPassFor(0x136) == 0x151, "skinned with vertex colours: _VcS");
	Check(ShadowPassFor(0x130) == 0x14E && ShadowPassFor(0x137) == 0x14F, "parallax, with and without vertex colours");
	Check(ShadowPassFor(0x133) == 0x14E && ShadowPassFor(0x134) == 0x150, "glow, plain and skinned");
	Check(ShadowPassFor(0x138) == 0x14F && ShadowPassFor(0x139) == 0x151, "glow with vertex colours, plain and skinned");
	Check(ShadowPassFor(0x13A) == 0x14E && ShadowPassFor(0x13B) == 0x150, "with the projected shadow: the same as without");
	Check(ShadowPassFor(0x142) == 0x14F && ShadowPassFor(0x143) == 0x151 && ShadowPassFor(0x146) == 0x151, "and with vertex colours");

	Section("Records that get none");
	Check(ShadowPassFor(0x12F) == 0 && ShadowPassFor(0x13C) == 0, "hair");
	Check(ShadowPassFor(0x131) == 0 && ShadowPassFor(0x132) == 0 && ShadowPassFor(0x13E) == 0 && ShadowPassFor(0x13F) == 0, "FaceGen");
	Check(ShadowPassFor(0x147) == 0 && ShadowPassFor(0x14E) == 0 && ShadowPassFor(4) == 0 && ShadowPassFor(0x156) == 0, "what is no lighting record");

	Section("Whose list gets shadow records");
	Check(IsMark(kMark) && !IsMark(0) && !IsMark(0x3F800000), "the mark is the probe's word, not a tint's 0 or 1.0");
	Check(Receives(true, kMark, true, 0x135) == 0x14F, "on, marked, shadows drawn, a lighting record: the pass");
	Check(Receives(false, kMark, true, 0x135) == 0, "off in OBNG.ini");
	Check(Receives(true, 0, true, 0x135) == 0, "the game's own Lighting30 geometry (no mark) stays as the game has it");
	Check(Receives(true, kMark, false, 0x135) == 0, "the game draws no actor shadows");
	Check(Receives(true, kMark, true, 0x12F) == 0, "a record that takes none");

	Section("The counts");
	Counts counts;
	Count(counts, 0x14F, 2, 4);
	Count(counts, 0x150, 0, 9);
	Count(counts, 0, 0, 7);
	Check(counts.lists == 3, "every lighting record of a marked property");
	Check(counts.eligible == 2, "those with a shadow pass");
	Check(counts.withShadow == 1 && counts.records == 2, "lists that got records, and how many");
	Check(counts.leftOut == 1 && counts.mostLights == 9, "a record with more lights than the shader draws, and the most seen");

	Section("Lights the shader does not draw");
	Check(!LightsLeftOut(1) && !LightsLeftOut(7), "the scene's light and six more are drawn by every program");
	Check(LightsLeftOut(8) && LightsLeftOut(20), "from the eighth on a light may be left out");
	Section("The flags seen on moved geometry");
	FlagCounts flags;
	CountFlags(flags, 0);
	Check(flags.lists == 1, "a list without any flag is a list");
	bool none = true;
	for (int i = 0; i < kFlagBits; ++i) {
		none = none && flags.bit[i] == 0;
	}
	Check(none, "and counts for no bit");
	CountFlags(flags, 0x4001);
	CountFlags(flags, 0x80000001);
	Check(flags.lists == 3 && flags.bit[0] == 2, "a bit set in two lists counts twice");
	Check(flags.bit[14] == 1 && flags.bit[31] == 1, "each bit by itself, the highest too");
	Check(flags.bit[1] == 0 && flags.bit[13] == 0 && flags.bit[15] == 0 && flags.bit[30] == 0, "its neighbours untouched");
	CountFlags(flags, 0xFFFFFFFF);
	bool all = true;
	for (int i = 0; i < kFlagBits; ++i) {
		all = all && flags.bit[i] >= 1;
	}
	Check(all && flags.lists == 4 && flags.bit[0] == 3, "all bits at once");
	return Failures();
}
