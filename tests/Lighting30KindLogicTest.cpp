// Every flow of the shader kind switch (src/engine/render/Lighting30KindLogic.h): the key's values, which of the
// game's choices the probe moves to Lighting30Shader, what it leaves alone, the card's flag, the counts.

#include "Check.h"
#include "engine/render/Lighting30KindLogic.h"

using namespace obng::engine::render::lighting30_kind;
using namespace obng::test;

int main() {
	Section("The key's values");
	Check(Shipped() == 0 && !On(BitsOf(Shipped())), "off as shipped");
	Check(BitsOf(1) == kMoveShadowLight && On(BitsOf(1)), "1 moves the geometry the game gives ShadowLightShader");
	Check(BitsOf(2) == kMoveSkin && BitsOf(4) == kMoveParallax, "2 the skin, 4 the parallax geometry");
	Check(BitsOf(7) == 7 && BitsOf(3) == (kMoveShadowLight | kMoveSkin), "several together");
	Check(BitsOf(8) == 0 && BitsOf(0x10 | 1) == kMoveShadowLight, "bits beyond the three are dropped");
	Check(BitsOf(-1) == 0 && BitsOf(-7) == 0, "a negative value is off");

	Section("Which choices move");
	Check(Moves(kShadowLight, kMoveShadowLight), "ShadowLightShader's geometry with bit 1");
	Check(!Moves(kShadowLight, kMoveSkin | kMoveParallax), "and not without it");
	Check(Moves(kSkin, kMoveSkin) && !Moves(kSkin, kMoveShadowLight | kMoveParallax), "SkinShader's with bit 2 only");
	Check(Moves(kParallax, kMoveParallax) && !Moves(kParallax, kMoveShadowLight | kMoveSkin), "ParallaxShader's with bit 4 only");
	Check(!Moves(kLighting30, 7), "what the game gives Lighting30Shader itself is not a move");
	Check(!Moves(0x11, 7) && !Moves(0x0D, 7) && !Moves(0x02, 7), "water, hair, grass: never");
	Check(!Moves(0, 7), "the chooser's 'no shader' stays");
	Check(!Moves(kShadowLight, 0) && !Moves(kSkin, 0) && !Moves(kParallax, 0), "no bits: nothing moves");

	Section("The kind a geometry gets");
	Check(KindFor(kShadowLight, true, kMoveShadowLight) == kLighting30, "moved: Lighting30Shader");
	Check(KindFor(kSkin, true, 7) == kLighting30 && KindFor(kParallax, true, 7) == kLighting30, "all three with all bits");
	Check(KindFor(kSkin, true, kMoveShadowLight) == kSkin, "a kind whose bit is not set keeps the game's choice");
	Check(KindFor(0x11, true, 7) == 0x11 && KindFor(kLighting30, true, 7) == kLighting30 && KindFor(0, true, 7) == 0, "other kinds pass through");

	Section("Without the card's flag");
	Check(KindFor(kShadowLight, false, 7) == kShadowLight, "no SM3 programs: the game makes no Lighting30Shader, so the choice stays");
	Check(KindFor(kSkin, false, 7) == kSkin && KindFor(kParallax, false, 7) == kParallax, "for every kind");

	Section("The counts");
	Counts counts;
	Count(counts, kShadowLight, kLighting30);
	Count(counts, kShadowLight, kShadowLight);
	Count(counts, kSkin, kLighting30);
	Count(counts, kParallax, kParallax);
	Count(counts, kLighting30, kLighting30);
	Count(counts, 0x11, 0x11);
	Check(counts.asked == 6, "every call of the chooser");
	Check(counts.shadowLight == 2 && counts.skin == 1 && counts.parallax == 1, "by the game's choice");
	Check(counts.lighting30 == 1 && counts.other == 1, "the game's own Lighting30 geometry and the rest");
	Check(counts.moved == 2, "moved: the result differs from the game's choice");
	return Failures();
}
