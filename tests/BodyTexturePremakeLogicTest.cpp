// Every flow of the body textures made on the loader's thread (src/engine/facegen/BodyTexturePremakeLogic.h).

#include <cstring>

#include "Check.h"
#include "engine/facegen/BodyTexturePremakeLogic.h"

using namespace obng::engine::facegen::premake;

int main() {
	Section("The INI's value");
	Check(!OnOf(0) && OnOf(1) && !OnOf(2) && !OnOf(-1), "1 is on, anything else is off");
	Check(Shipped() == 0, "off as shipped until the game's gates have run");

	Section("The parts a body texture is asked for (0x0047AC20's switch, read)");
	Check(kPartCount == 5, "five");
	Check(kParts[0] == 2 && kParts[1] == 3 && kParts[2] == 4 && kParts[3] == 5 && kParts[4] == 15, "upper body, lower body, hand, foot, tail");

	Section("Whether a queued character's textures are made on the loader's thread");
	const Facts good{true, kNpcFormType, true, true, true, false, false};
	Check(Decide(good) == Skip::None, "an NPC with a race, texturing on, on the loader's thread, not made yet: made");
	Facts f = good;
	f.npc = false;
	Check(Decide(f) == Skip::NoNpc, "a task without an NPC: skipped");
	f = good;
	f.formType = 0x24;
	Check(Decide(f) == Skip::NotAnNpc, "a creature: skipped");
	f = good;
	f.race = false;
	Check(Decide(f) == Skip::NoRace, "an NPC without a race: skipped");
	f = good;
	f.texturing = false;
	Check(Decide(f) == Skip::TexturingOff, "bFaceGenTexturing off: the game makes no such texture, skipped");
	f = good;
	f.loaderThread = false;
	Check(Decide(f) == Skip::NotTheLoaderThread, "another thread than the loader's (a texture miss would pause the loader from inside a task): skipped");
	f = Facts{false, 0x24, false, false, false, true, true};
	Check(Decide(f) == Skip::NoNpc, "the first failing test names the skip");
	f = Facts{true, 0x24, false, false, false, true, true};
	Check(Decide(f) == Skip::NotAnNpc, "then the form type");
	f = Facts{true, kNpcFormType, false, false, false, true, true};
	Check(Decide(f) == Skip::NoRace, "then the race");
	f = Facts{true, kNpcFormType, true, false, false, true, true};
	Check(Decide(f) == Skip::TexturingOff, "then the setting");

	f = good;
	f.alreadyMade = true;
	Check(Decide(f) == Skip::AlreadyMade, "both textures in the map already: nothing to make");
	f = Facts{true, kNpcFormType, true, true, false, true, true};
	Check(Decide(f) == Skip::NotTheLoaderThread, "the thread is tested before the map");

	f = good;
	f.mainThreadInside = true;
	Check(Decide(f) == Skip::MainThreadInside, "the lock not free at once (the main thread is in the body texture call): skipped, never waited for");
	f = Facts{true, kNpcFormType, true, true, true, true, true};
	Check(Decide(f) == Skip::AlreadyMade, "the map is tested before the lock");

	Section("The name an NPC's own texture is kept under");
	char key[kKeySize];
	Check(KeyOf(2, true, 0x0001A2B3u, key) && std::strcmp(key, "0001A2B3ModUpperBodyFemale") == 0, "the upper body of a woman");
	Check(KeyOf(2, false, 0x0001A2B3u, key) && std::strcmp(key, "0001A2B3ModUpperBodyMale") == 0, "the upper body of a man");
	Check(KeyOf(3, true, 0xFF00000Fu, key) && std::strcmp(key, "FF00000FModBody") == 0, "the lower body: one name for both sexes, eight digits");
	Check(KeyOf(4, false, 1, key) && std::strcmp(key, "00000001ModBody") == 0 && KeyOf(5, false, 1, key) && KeyOf(6, false, 1, key)
	          && KeyOf(15, false, 1, key) && std::strcmp(key, "00000001ModBody") == 0,
	      "hand, foot, the fifth slot and the tail share it");
	Check(!KeyOf(1, false, 1, key) && key[0] == '\0' && !KeyOf(7, false, 1, key) && !KeyOf(14, false, 1, key) && !KeyOf(16, false, 1, key)
	          && !KeyOf(0, false, 1, key) && !KeyOf(-1, false, 1, key),
	      "any other part has no texture of its own");
	Check(std::strlen("FFFFFFFFModUpperBodyFemale") < kKeySize, "the longest name fits");

	Section("The totals");
	Totals totals;
	Count(totals, Skip::None, 500, 2);
	Count(totals, Skip::None, 200, 0);
	Count(totals, Skip::NotAnNpc, 0, 0);
	Count(totals, Skip::NotTheLoaderThread, 0, 0);
	Count(totals, Skip::NotTheLoaderThread, 0, 0);
	Check(totals.made == 2 && totals.ticks == 700 && totals.longest == 500, "made: count, sum and longest of their time");
	Check(totals.withTexture == 2, "the textures the calls came back with are summed");
	Check(totals.skipped[static_cast<int>(Skip::NotAnNpc)] == 1 && totals.skipped[static_cast<int>(Skip::NotTheLoaderThread)] == 2
	          && totals.skipped[static_cast<int>(Skip::NoRace)] == 0,
	      "skips are counted by their reason");
	Check(totals.skipped[static_cast<int>(Skip::None)] == 0, "a made one is not a skip");
	Count(totals, Skip::AlreadyMade, 0, 0);
	Check(totals.skipped[static_cast<int>(Skip::AlreadyMade)] == 1 && totals.made == 2, "an NPC whose textures were there is counted apart");
	Count(totals, Skip::MainThreadInside, 0, 0);
	Check(totals.skipped[static_cast<int>(Skip::MainThreadInside)] == 1 && totals.made == 2, "and one skipped for the main thread");
	return Failures();
}
