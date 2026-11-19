// Every flow of the texture prepare's lock skip (src/engine/render/TextureLockSkipLogic.h).

#include "Check.h"
#include "engine/render/TextureLockSkipLogic.h"

using namespace obng::engine::render::texture_lock_skip;

int main() {
	Section("Which way a prepare takes");
	Check(WayOf(0x1A2B3C40u) == Way::Unlocked, "the texture has its renderer data: no lock");
	Check(WayOf(0) == Way::Locked, "no data yet: the original's way under the lock, which creates it");

	Section("The check: the pointer without the lock against the pointer under it");
	Check(Agrees(0x1A2B3C40u, 0x1A2B3C40u), "the same data both ways");
	Check(!Agrees(0x1A2B3C40u, 0x22220000u), "other data under the lock: a mismatch");
	Check(!Agrees(0x1A2B3C40u, 0), "data without the lock, none under it: a mismatch");
	Check(Agrees(0, 0x1A2B3C40u) && Agrees(0, 0), "null without the lock: the lock is taken anyway, nothing to agree");
	return Failures();
}
