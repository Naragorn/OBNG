// Every flow of the pass mask's alternation (src/engine/render/PassMaskAlternateLogic.h): the key's values, which
// bits it may flip, the flip itself, which side a phase is on, the counts of a window.

#include "Check.h"
#include "engine/render/PassMaskAlternateLogic.h"

using namespace obng::engine::render::pass_mask_alternate;
using namespace obng::test;

int main() {
	Section("The key's values");
	Check(Shipped() == 0 && !On(BitsOf(Shipped())), "off as shipped");
	Check(BitsOf(8) == kSpecular && On(BitsOf(8)), "8 is the specular pass");
	Check(BitsOf(0x40) == kRebuild, "0x40 alone builds every list anew and changes none: the control");
	Check(BitsOf(0x4F) == 0x4F && BitsOf(3) == (kAmbient | kDiffuse), "several bits together");

	Section("Bits it does not flip");
	Check(BitsOf(0x10) == 0 && BitsOf(0x20) == 0 && BitsOf(0x80) == 0, "0x10 is read by GetRenderPasses for something else, the rest is not known");
	Check(BitsOf(0x18) == kSpecular, "an allowed bit beside a refused one stays");
	Check(BitsOf(-1) == 0 && BitsOf(-8) == 0, "a negative value is off");
	Check(BitsOf(0x100 | 8) == kSpecular, "bits beyond the mask's byte are dropped");

	Section("The flip");
	Check(Flip(0x0F, kSpecular) == 0x07, "all four passes: the specular bit goes");
	Check(Flip(0x07, kSpecular) == 0x0F, "and comes back at the next switch");
	Check(Flip(0x1F, kSpecular) == 0x17 && Flip(0x0F, kRebuild) == 0x4F, "the other bits stay as they are");
	Check(Flip(Flip(0x0F, 0x4F), 0x4F) == 0x0F, "two switches give the game's mask again");
	Check(Flip(0x0F, 0) == 0x0F, "no bits: no change");

	Section("The side of a phase");
	Check(!Flipped(0), "the run starts with the game's mask");
	Check(Flipped(1) && !Flipped(2) && Flipped(3), "every switch changes the side");
	Check(Flipped(1001) && !Flipped(1000), "after many switches too");
	return Failures();
}
