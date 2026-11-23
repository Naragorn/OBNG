# An NPC's arrival: FaceGen's cache and locks, the body textures, the animation set up

What the main thread spends when an NPC comes into view, beyond the files the loader reads for it: the end of its
queued character task attaches the actor's 3D (the skeleton's model, the animation set up, the body with its
textures), and FaceGen's model cache stands between that and the loader's thread. Marks as in AGENTS.md: read,
measured, derived, unverified. The frame around it (the pump, the pose wait, a crossing) is in
`knowledge/background-loading.md`.

## FaceGen's model cache and the main thread's wait on its lock (2026-11-20, read and measured)

**What the 4 ms of a body texture are (measured, `obng-measure-body-texture.ini`, run
`obng-profile-ride-anvil-late/20261120-160018`).** Not the synthesis loops: of 90 ms a ride in 0x0052D2C0 (117 calls),
59 ms are the model's fetch 0x00553620 (24 calls; 5 - 14 ms in six of them, else microseconds), 28 ms the synthesis
0x00557E60, and of those 25.5 ms are 0x0054DA40 (the float image made into a texture, 0.5 - 4.1 ms a call). The
accumulate loops at 0x005582CA are about 0.1 ms a call - a faster loop would gain nothing.

**The cache (read).** `[[0x00B39B80] + 0xDAC]`, made on first use with two limits copied from the settings
uiFaceGenMaxEGMDataSize (value at 0x00B120EC -> cache +0x18, the shape modes of the .egm files) and
uiFaceGenMaxEGTDataSize (0x00B120F4 -> +0x1C, the texture modes of the .egt files), 64 MB each as shipped and in
Nadi's INI. An entry holds two data objects (+8 the EGM's, +0xC the EGT's: the path at [0], the data at [2]). The
fetch 0x00553620 finds the entry (0x005515B0) or makes and inserts one (0x00559B50, 0x00551450). **Every find and
insert takes the cache's lock 0x00B39C00 and, holding it, runs the purge 0x005506B0** - for each kind, while the
data (summed by a walk of the whole map, 0x0054F890) is over the limit, 0x0054FC30 drops the oldest entry's data, the
entry stays - **and then loads the entry's missing data from its files, still under the lock** (0x00559190 ->
0x00558770 for the EGM, 0x00558520 -> 0x00557C80 for the EGT). `bLoadBackgroundFaceGen` (0x00B1210C) is read by
nothing but its own initialiser. Guides (Step Mods' INI guide) give the two limits' defaults and no description.

**Measured (`obng-measure-facegen-cache.ini`: the cache's functions timed on the main thread, [Probe] LockCalls;
runs `-160558`, `-161036`, `-163524`).** The fetch's time is the find's, and the find's is the wait for the lock:
three to five waits a ride of 4.7 - 13.0 ms at 0x005515E2, each in the frame a queued character's body texture is
made; the holder's last enter site is the purge's 0x005506C3 (once the size walk's 0x0054F8D0). The main thread's own
reloads are small (2 EGM loads 3.8 - 5.2 ms, 3 EGT loads 1.4 - 1.9 ms a ride), its purges 2 ms in 54 calls, one
eviction. Nadi's mod list has 415 loose .egm files of 511 MB and 26 loose .egt files of 161 MB (a head's .egt 9.4 MB).

**`[Route] FaceGenCacheEgmMB` / `FaceGenCacheEgtMB` measured: the waits stay** (plugin/src/FaceGenCacheBudget.cpp,
src/engine/facegen/FaceGenCacheBudgetLogic.h; the two limits raised at the first update step, never below the game's;
OBNG.dll 4877e1fe, `obng-measure-facegen-budget.ini` with 512 / 256 MB against `obng-measure-facegen-cache.ini`, one
ride each, `-163158` / `-163524`):

| | limits 512 / 256 MB | the game's 64 / 64 MB |
|---|---:|---:|
| the cache's data at the ride's end: shapes / textures | 42 / 94 MB | (not read) |
| evictions on the main thread | 0 | 1 |
| the main thread's waits on the lock | 5: 6.5 - 13.3 ms (52 ms) | 3: 4.8 - 9.5 ms (20 ms) |
| committed memory at the end | 755 MB | 725 MB |

So a ride's texture modes (94 MB) are over the game's 64 MB and get dropped and read again, but that is not what the
main thread waits for: with nothing dropped it waits as long. Derived (the probe keeps a holder's last enter site
only, and the purge is the last enter of a find): the holder is another thread inside a find or an insert, reading a
model's files under the lock - a head's 9.4 MB .egt on the loader's thread. Not proven on that thread then;
measured since (the next section). The route stays off (`kShippedEgmMb` / `kShippedEgtMb` 0): no gain for the main
thread, 30 MB more memory. Levers left (nothing built): the file read moved out of the lock in 0x005515B0 (needs a
per-entry "being loaded" state); or the body texture made a frame later when the lock is held (needs the skin applied
again, 0x0047AC20 from 0x0047B090 / 0x0047BC40).

**A queued character's end, second split** (`obng-measure-char-finish-2.ini`, run
`obng-profile-ride-anvil-late/20261120-171759`, 19 ends over 3 ms in the ride; read: 0x00441EF0 gets the actor's 3D
through its vtable +0x14C, 0x00659F30 -> the reference's 0x004E4080 -> the base form's slot 0x114, for an NPC
0x00521A40): the unnamed 3.5 - 5 ms of every end are the actor's 3D made on the main thread -
- the skeleton's model made for the reference, 0x004B3750 under 0x00521A40: 0.9 - 1.6 ms;
- the animation set up 0x004E3490 without the body: about 2 - 3.5 ms (323 model lookups 0x00439FF0 of 0.1 ms among
  them, so the files are cached; the rest is the set up's own work);
- the body 0x00526DB0 -> 0x0047BC40 -> 0x0047B5B0 -> 0x0047B090 -> the skin parts 0x0047AC20 -> the body texture
  0x0052D2C0: 0.4 - 1.2 ms, and 10.5 - 12.3 ms in the three ends where the body texture waited on FaceGen's lock.
Over the whole run: 0x004E4080 1 863 calls, 472 ms; the animation set up 1 288 calls, 349 ms; the NPC's model 38
calls, 47 ms; a creature's model 0x0051C640 79 calls, 32 ms. So an end is 3.4 - 6.3 ms of the engine's own
instantiation (it fits a 75 Hz frame beside 2 ms of update, not a 90 Hz frame for the larger ones), and what breaks
a 75 Hz frame is the wait on FaceGen's lock.

## Who holds the cache's lock: the loader's thread, in two ways (2026-11-20, measured and read)

The derived holder above is measured now (`[Probe] FaceGenLoads`, plugin/src/FaceGenLoad.cpp: the finds' four calls
of the loads timed by kind and thread; runs `obng-profile-ride-anvil-late/20261120-175137`, `-183808`):

- **A model's data files are read by the first find of it, under the lock.** A model is made with its mesh and the
  two files' paths only (0x00559330, 0x00556A00); the find - 0x005515B0 (both kinds), 0x005516B0 (the shape modes,
  reached through 0x00551930), 0x005517F0 (the texture modes, through 0x00551960) - calls 0x00559190 and 0x00558520
  after its purge, and those read the file when the model has no data. On the loader's thread a ride reads 82 - 86
  .egm files in 268 - 330 ms (a head's 8 - 22 ms) and 31 - 40 .egt files in 226 - 301 ms (a head's 5 - 51 ms), many of
  them again and again: the same head's files come back three and four times a ride, because the purge dropped
  them (the 64 MB limits) - which is what `FaceGenCacheEgmMB` / `EgtMB` would spare the loader, not the main thread.
- **The head's texture synthesis holds the lock for all of itself.** 0x005551C0 (the head's FaceGen textures;
  QueuedHead's run on the loader's thread reaches it through 0x00523220 -> 0x0052DED0) wraps each call of the
  synthesis 0x00557E60 in the cache's lock (`mov ecx, 0x00B39C00` before the enter 0x0043F2E0 at 0x00555483 and the
  leave 0x0043F300 at 0x0055549B; again at 0x00555509 / 0x00555525): the find with its .egt read, the synthesis and
  the texture's making 0x0054DA40. The body textures' path 0x0052D2C0 calls the same synthesis with no lock.

Other holders (read): 0x00527860 (the NPC's age morph, called at a character's end on the main thread) holds the
lock across its loop of morphs 0x00558840; the finds inside it are entered a second time. A second lock,
0x00B39C80, is FaceGen's data lock: QueuedHead's run holds it for all of the run ("QueuedHead::Run()", 0x0043530D),
QueuedHelmet's poll takes it around its morph (0x0043EDC4), and the evictions only try it (0x00401030 in
0x0054FC30, 0x0054F9C0, 0x0054FB00) - nothing is evicted while a head is being made. The data's users take the
model's own flags around each use (+0x14 the shapes', +0x18 the textures'; 0x00558840, 0x00557E60) and check the
data again under the flag; an eviction frees under the same flag (0x00559BA0, 0x00559C40); a user that finds the
flag taken returns without having done its work (0x00557F64: no texture).

**The threads (measured, run `-182603`):** the main thread is 432 in these runs, the loader's 584 - the id at
`[[0x00B33A10] + 0x30]`, the one the texture load 0x00442890 loads on without the pause. OBSE loads its plugins on
neither (436). An IO task's slot 1 is its run on the loader's thread (QueuedHead 0x00435300); a queued character or
reference has none (0x0060D0A0): its slot 11 (0x0043DC00, 0x0043C9B0) queues its children on the main thread, slot
10 is the pump's poll, slot 5 the end.

## `[Route] FaceGenLoadOutside`: the reads and the head's synthesis off the cache's lock (class B, measured)

plugin/src/FaceGenLoad.cpp, src/engine/facegen/FaceGenLoadLogic.h, tests/FaceGenLoadLogicTest.cpp. Two changes
behind one key:

1. The finds' four calls of the loads (0x0055163C, 0x00551643, 0x00551781, 0x005518C1) go to OBNG. When the load
   would read a file and the lock is entered exactly once (the engine's depth word 0x00B39C7C and the critical
   section's count both 1), the lock is given up for the read and taken again after it; loads of one model take
   turns on one of sixteen locks of OBNG's, so no file is read twice at once. Otherwise the call is the game's.
2. The head synthesis's four `mov ecx, 0x00B39C00` (0x0055547E, 0x00555496, 0x00555504, 0x00555520) name a lock of
   OBNG's in the engine's shape instead: the syntheses of 0x005551C0 still take turns among themselves and no longer
   hold up the cache. Without this, 17 of 107 loads sat under a lock entered twice and the main thread still waited
   5 - 11 ms (run `-175504`, the reads alone).

What it leans on (read): the data pointer is already written and read across threads without the cache's lock (the
users' flags; both users read a file with no lock at all when the model is not in the cache), and the body path runs
the synthesis without the lock. What changes: between a find's return and the user's flag an eviction on another
thread is no longer shut out for the head's path - as it never was for the body's; the just-stamped entry is the
last the purge would pick.

Measured (the ride towards Anvil with OBVR's late pose wait, Linux, no SteamVR, stand-in Beyond at 75 Hz, exclusive,
`tools/re/facegen-ride.py`; "off" is OBNG.dll with the route's key at 0):

| run | route | main-thread steps waiting on the cache's lock (ms each) | queued characters' ends: ms in all / longest | frames over 14.5 ms |
|---|---|---|---:|---:|
| `-175137` | off | 1 (13.7) | 390 / 42.5 | 31 |
| `-183808` | off | 2 (11.6, 10.4) | 439 / 28.2 | 43 |
| `-181126` | off (a build beside it: frames not usable) | 3 (12.3, 11.4, 7.5) | 527 / 45.4 | - |
| `-175504` | the reads only | 4 (10.7, 10.6, 5.3, 1.1) | 305 / 30.9 | 39 |
| `-185026` | on | 0 | 323 / 22.6 | 59 |
| `-185353` | on | 0 | 357 / 34.6 | 40 |
| `-185719` | on | 0 | 326 / 17.1 | 45 |

The waits are gone in every ride with the route (and in `-182603`, a fourth, with the timed list). All 115 - 120
reads of a ride run outside the lock, none under a lock entered twice. The ride's count of frames over 14.5 ms does
not show it: 40 - 59 against 31 - 43, one to four frames in a spread of twenty - the pump's other ends, the grid's
move and the scripts hold the rest (background-loading.md). Not covered: real SteamVR, 90 Hz, Windows, a long
session, Nadi's look. Off as shipped (`Shipped()` 0) until those gates have run.

## The animation set up 0x004E3490 by its parts, and the engine's own spreading (2026-11-20, read and measured)

**Read.** 0x004E3490 makes the actor's animation data (0x00473EB0, 0xDC bytes), the body (0x004E1580 -> for an NPC
0x00526DB0), gets the skeleton folder's KF files as a fresh list of copied names (0x00435830: from a kept list per
folder, 0x00521190, else a directory listing 0x00431970) and hands it to 0x00475D80. That finds five named nodes
(0x004D96F0), makes the controller manager (0x006C5610) and, for each file, looks its model up (0x00439FF0) and
calls 0x00474070, then frees the name and the node. 0x00474070 finds or makes the group's entry in the animation
data's map (0x00470960, 0x00470820) and either makes the sequence at once (0x0049FD90, added to the manager with
0x006C5760) or - in play - appends the file to the data's list at +0xB4 (0x0067B1E0) to be cloned later: 0x00476D10,
each actor's animation update, clones while a global count 0x00B33C28 is under the setting
**iAnimationClonePerLoop:BackgroundLoad** (0x00B06548; 5 as compiled and in Nadi's INI). At once only: group 0 (idle),
0x20 and, in one menu, 0x21; and everything when the player has no 3D yet, in menu mode, or for the player's own
nodes. So the engine already spreads an NPC's 300 sequences over frames; what a set up costs is the list work.

**Measured** (`obng-measure-facegen-load.ini`, run `-175137`, an NPC with 323 KF files in the ride, the timed
list's own cost in the numbers): a set up of 3.1 - 4.3 ms is

| part | ms |
|---|---:|
| the body 0x004E1580 (its textures 0x0052D2C0 0.6 - 1.6 of it: the synthesis 0.5 - 0.8, the fetch up to 1.0) | 1.1 - 2.3 |
| 0x00474070, 290 - 295 calls: 273 appends 0x0067B1E0 0.27 - 0.45, 17 - 22 sequences made 0x0049FD90 0.26 - 0.41 and added 0x006C5760 0.14 - 0.24, the rest its own | 1.0 - 1.7 |
| the list of names 0x00435830 (646 allocations) | 0.3 - 0.4 |
| the 323 model lookups 0x00439FF0 | 0.1 - 0.15 |
| the manager 0x006C5610 | 0.1 |
| not named | 0.3 |

Over the whole run: 0x00474070 21 723 calls 397 ms, the sequences made 13 523 times 235 ms (17 us each) and added
125 ms (9 us each); the animation update 0x00476D10 340 248 calls. A first set up of a kind pays its KF files
besides (323 lookups 4 - 21 ms once).

**`[Route] AnimationLazyAppend` (class A, built, measured in part).** The append 0x0067B1E0 walks the list from its
head each time: 37 000 steps for 273 appends. plugin/src/AnimationLazyAppend.cpp replaces the call at 0x00474348
and keeps the list's last node between the appends of one set up (the begin and end come from AnimationPauseBatch's
detour of 0x00475D80; outside a set up the walk is the game's); src/engine/io/AnimationLazyAppendLogic.h,
tests/AnimationLazyAppendLogicTest.cpp (the same nodes as the game's append, node for node). In the game (runs
`-185026` to `-185719`): 9 740 appends a run, 9 312 of them from the kept end, 5 382 nodes passed in all; the set
up's 0x00474070 share 0.6 - 1.0 ms with it against 1.0 - 1.7 (run `-182603`, three set ups - a small sample). Off as
shipped until measured over more set ups.

**Not built: the set up on the loader's thread.** It was the plan (lever C); the reading says no. A queued
character has no run on the loader's thread to put it in, the 3D it would make is attached, given Havok bodies and
entered into the process lists on the main thread, and the engine already defers what is large (the sequences).
What is left to take is about 1 ms of list work an NPC: the names' copy 0x00435830 with its 646 allocations and
frees, and the map's bookkeeping in 0x00474070 - a rebuild of 0x00475D80's loop (class A), not a thread move.

## The body textures made ahead: `[Route] BodyTexturePremake` (class B, built)

Read: the body textures need the NPC's form and the race only (0x0052D2C0 is `__thiscall(race, base out, own out,
npc, part)`, RET 0x10; the only call in the EXE is the skin parts' at 0x0047ADFF) and are kept in the texture map
`[0x00B35300]` by the NPC's form id - "%08XModUpperBodyFemale" / "...Male" for the upper body, "%08XModBody" for
lower body, hand, foot and tail. The map's find 0x004A1D80 takes its own lock 0x00B35380. So they can be made
before the character's end asks for them, on the loader's thread, after the NPC's queued head has run
(plugin/src/BodyTexturePremake.cpp, src/engine/facegen/BodyTexturePremakeLogic.h).

**Why the first three forms froze the game: Blockhead reads the engine caller's stack (measured, `[Probe]
HangWatch`, run `-191622`).** Blockhead (shadeMe's OBSE plugin, in Nadi's mod list; its source, BodyOverride.cpp)
hooks the race texture's path in 0x0052D2C0 at 0x0052D4C9 and takes the body part and the NPC from `[ebp - 0x1C]`
and `[ebp - 0x20]`. In that function EBP is the base out pointer (0x0052D302), so these are words of the caller's
frame: at the engine's call (0x0047ADF3 - 0x0047ADFF: push part, push npc, push &own, push &base) the base local
sits 0x18 above the stack top the arguments are pushed from, and the two words are the pushed part and NPC. Called
from OBNG's C++ they were whatever lay under OBNG's local: Blockhead's assertion failed and its message box stood
on the calling thread for good - the hang watch shows the loader's thread in user32 under Blockhead.dll with
"Assertion failed" on its stack, holding OBNG's lock, and the main thread in the loader's pause (0x0043231A <-
0x00432865). The same explains the earlier freezes (the save that did not load in `-180702`, two of three rides
with OBNG.dll 1bd87069: `-182929`, `-184134`), where OBVR's watchdog named the main thread only - once in the
pause, once waiting on a lock inside Blockhead.dll, which the thread with the message box held. The second hook,
0x0052D612 (the body's .egt), takes its values from the function's own frame (EBP is the part again from
0x0052D5AF on) but passes the path through one static TESModel written at every call.

What the route does about it:

- OBNG calls 0x0052D2C0 from a thunk with the engine's stack shape (the base local 0x18 above the pushed
  arguments), and its wrapper of the engine's own call at 0x0047ADFF is a thunk that leaves the engine's pushed
  arguments where they are and hands the function copies.
- The function never runs on two threads at once (Blockhead's static, and the synthesis gives up when another
  thread has the model's flag): the main thread's call and the premake take turns on a lock of OBNG's around the
  whole function.
- The premake only tries that lock and skips the NPC when the main thread is inside. Derived, not seen in a run:
  the race texture's load 0x00442890, missed on the main thread, pauses the loader until its running task ends
  (0x00432860 -> 0x004322B0), and a task that waited for the lock would never end.
- The premake runs after the queued head's run (QueuedHead's vtable slot 1 at 0x00A36CE8, the NPC at task +0x20),
  only on the thread the texture load takes for the loader's.

A general lesson for calls into the engine from OBNG: another plugin's hook may depend on the engine caller's
frame. A function with one call site in the EXE is a candidate; its hooks are worth reading before it is called
from anywhere else.

**Measured with that form in** (OBNG.dll 68e6b3c2; all three routes on: runs `-192338`, `-192706` plain and `-193032`
with the timed list; `-193357` off in the same block; `tools/re/facegen-ride.py`, the timed totals, [Probe]
TaskEnds):

| | routes off | FaceGenLoadOutside + AnimationLazyAppend | all three |
|---|---:|---:|---:|
| NPCs' body textures made on the loader's thread, a run | - | - | 40 - 44 in 56 - 62 ms (longest 3.1 - 3.8) |
| the main thread in 0x0052D2C0, a run (timed) | 86 calls, 80.5 ms, longest 19.5 | - | 105 calls, 1.6 ms, longest 0.03 |
| the synthesis 0x00557E60 on the main thread, a run | 24 calls, 42.6 ms | - | 0 calls |
| a queued character's end 0x0043B090, a run (timed) | 40 calls, 410 ms, longest 42.5 | - | 43 calls, 315 ms, longest 17.2 |
| queued characters' ends of 6 ms or more in the ride: count, ms | 9 - 10, 85 - 130 | 4 - 6, 47 - 48 | 2, 19 - 38 |
| main-thread steps with 2 ms or more of lock waits in the ride: count, ms | 6 - 11, 40 - 77 | 4 - 7, 21 - 25 | 2 - 3, 9 - 19 |

Seven NPCs a run were in the map already, none was skipped for the main thread being inside. Three rides and the
save's load passed; the hang watch wrote nothing after the loading screen.

**What the ride's late frames say: no difference shown, either way.** Frames over 14.5 ms in the ride, present to
present from OBVR's capture: off 43, 40; the two routes 40, 45; all three 44, 60, 58. The sum of their lateness over
13.4 ms: 460, 487 ms against 365, 448 against 508, 531, 467; frames over 25 ms: 27, 27 against 20, 27 against 25, 28,
24. The higher counts of two rides are the ride's first ten seconds (21 marginal frames there against 2 - 13 in the
others), before any NPC arrives. It is not the locks (the main thread's waits fall, the table above) and not the
heap's lock (300 - 330 ms of waits a run on all threads in every one of the nine rides). The rides are not the same
ride: the mod list's leveled spawns and schedules change who is on the road - in `-193032` the animation update made
2 478 sequences in twenty seconds at 100 - 120 us each, where every other interval of every timed run has 13 - 25 us.
The routes take two to nine late frames a ride out of forty to sixty; three rides a side cannot show that. The
numbers of what the routes touch can. Open: a scenario with a fixed set of arriving NPCs (the long session's city
legs), or many more rides a side.

## The gates run so far, and the state in the two games (2026-11-20 evening)

- **A long session** (`obng-long-memory.txt`, sixteen minutes, the ride and nine city legs, all three routes on, run
  `obng-long-memory/20261120-200042`): passed, 22 marks, no crash, no dump of the hang watch after the loading
  screen. It exercised the routes less than a ride does: 118 FaceGen reads, 30 premakes, 4 694 appends, all in its
  first minutes - the city legs brought no new queued heads.
- **AnimationLazyAppend checked in the game against the game's own walk** (`[Route] AnimationLazyAppend=2`,
  `obng-ride-lazy-append-check.ini`, run `obng-profile-ride-anvil-late/20261120-201719`): 9 203 appends compared,
  0 mismatches; the game's walk passed 1 156 921 nodes for the run's 9 791 appends, the route passes 5 000 - 9 000.
  That is the class A gate: verified.
- **In the two games** since 2026-11-20 20:21 (OBNG.dll 4e8c2903, backups
  `artifacts/backups/20261120-2021-npc-arrival-routes-on`): `[Route] FaceGenLoadOutside=1`, `BodyTexturePremake=1`,
  `AnimationLazyAppend=1` and `[Probe] HangWatch=1` in both OBNG.ini files, for Nadi's look and the peers' runs. The
  code's own default stays 0 until the two class B routes are accepted. Open for them: Nadi's look in the headset,
  real SteamVR, 90 Hz, Windows, the peer sessions' regression runs.
- **A crash to keep in mind** (Walkers' run of 19:44 with OBNG.dll 68e6b3c2, the three routes off by its INI): the
  pool allocator at 0x004025D2 unlinking a free 12-byte block whose link words had been overwritten, on the loader's
  thread while it read a NIF. Not reproduced in the next run. Nothing ties it to OBNG, Walkers or the game yet.


## `[Route] AnimationNameArena` (class A, verified, on in both games since 2026-11-22)

plugin/src/AnimationNameArena.cpp, src/engine/io/AnimationNameArenaLogic.h, tests/AnimationNameArenaLogicTest.cpp.
The two calls of 0x00435830 in the set up 0x004E3490 (0x004E38C5 an NPC's, 0x004E3B94 a creature's) go to OBNG's
function: the folder's kept list (0x00521190 on the table at 0x00B362C0) copied into a block of the thread's own
(256 KB) in the order and shape the game makes it - from the last kept name to the first, a head and a node of
8 bytes a name. The three frees of the list in 0x00475D80 (0x00475F9D a name, 0x00475FB8 a node, 0x00475FEF the
head) leave what lies in the block alone; the block is given back at the head's free or when the set up returns
(AnimationPauseBatch's detour says when). Without a kept list, or with the block full, the game's own function runs.

- **Verified** (the check `AnimationNameArena=2`, both lists made and compared name for name; runs
  `obng-npc-arrivals/20261122-203519` and `obng-profile-ride-anvil-late/20261122-203824`): 237 lists, 0 mismatches.
- **Measured in the same runs**: the game's copy 149 - 214 us a list (84 and 154 lists, 18.0 and 22.9 ms a run),
  OBNG's 4.0 - 5.4 us. As the route (runs `obng-npc-arrivals/20261122-205020`, `-205922`): 120 and 87 lists, all
  given back, 26 826 and 27 486 frees left alone, both runs passed. The frees' own saving is not measured apart.

## The arrivals scenario (2026-11-22): not yet a ruler

`tools/hand-scripts/obng-npc-arrivals.txt` places thirty named NPCs of Oblivion.esm one every three seconds behind
the player (before the eyes thirty figures made every second frame late by themselves, run `-203519`);
`tools/re/npc-arrivals.py` compares runs arrival by arrival against each window's median frame. Five runs
(`obng-npc-arrivals/20261122-205020` to `-210223`): frames more than 3 ms over the median 314 - 386 a run in four
of them (815 in one, whose median frame doubled by the end - not looked into), about ten an arrival; the queued
characters' ends 230 and 318 ms a run with every route on, 290 as played before today, 277 and 408 with the three
arrival routes off. The spread between two runs of one setting is as large as the difference between settings, so
the scenario shows no gain of any route in frames. What an arrival's ten late frames are made of is the next
thing to split (the Timed list on one arrival), before more runs.

## What an arrival's late frames are made of (2026-11-22, measured, Linux stand-in headset, not comparable with Windows)

`obng-measure-arrival-split.ini` (run `obng-npc-arrivals/20261122-214509`, the update step's spikes, 433 late frames
more than 3 ms over the window median against 2 174 normal ones), `obng-measure-arrival-render.ini` (run `-215926`)
and OBVR's capture of the same runs read with `tools/re/frame-waits.py`.

**A late frame is a missed interval, not 13 ms of work (measured).** The stand-in paces at 75 Hz: of 7 921 frames
7 021 are one interval long (13.33 ms) and 806 two, almost nothing between. Inside the render call sits OBVR's late
pose wait: 4.0 ms in a frame of one interval, 10.4 ms in a late one - the frame overran and waited for the next
interval. (A first reading of the same numbers took the render's 8.7 ms "in no timed function" for the GPU; it is
this wait.) The work a frame - its length less the pose wait - is 9.4 ms near the median and about 16 in a late
frame: **+3.6 ms update (the actors' update +2.3, of it the animation +1.0; the arrival's own parts - the
character's end, body, animation set up, name list - +0.6 together) and +3.2 ms render work (the scene pass +1.6)**
against a slack of 4 ms. The routes of this file work on the 0.6.

**Why the scenario is no ruler in frames (measured).** The count of late frames is a cliff: a frame 0.1 ms over
the slack counts like one 10 ms over. And the work a frame differs between runs of one setting by more than any
route gives: median work 9.4, 8.0, 10.5, 8.3, 7.7 ms in the five runs `-205020` to `-210223`, 10.3 and 11.5 in the
two timed ones (both beside DMCS's B&S harness, which `--exclusive` does not see; the Timed list costs too). For a
route worth 0.2 ms an arrival the evidence is the timed call (microseconds, in one run); frames can show it only
with both settings inside one run (a route switched by the script between arrivals - not built) or on a machine
nothing else uses.

## A crash at the lazy clone list: the queued item is held raw (2026-11-23, read; one crash, cause not settled)

Walkers' fight run `OblivionWalkers/artifacts/hand-script/necro-daedra/20261122-223536` (all five arrival routes on)
crashed in InterlockedIncrement called at 0x004740AC, return 0x004740B2, esi = 0x1B0: 0x00474070 reading [item+8]
of the item 0x00476D10's clone loop took from the head of the animation data's list (+0xB4 item, +0xB8 next; the
actor a temporary creature of Maskar's Oblivion Overhaul). Read in the pseudocode (instructions not checked): the
append at 0x00474348 stores the item's pointer and nothing else - 0x00474070 holds a reference on [item+8] only
for its own call - so an item freed while it waits in the list is read from freed memory at the pop. That would be
the engine's own lifetime bug; who frees a queued item is not read. Of the 76 logs in the Linux game's Crash Logs
this is the only one with 0x004740B2. `[Route] AnimationLazyAppend` writes this list (the call at 0x00474348): no
pop runs inside a set up (0x00475D80's callees do not reach 0x00476D10; read), and its check compared 9 740 appends
with the game's walk without a mismatch, but it is the one route that touches the list - if the crash returns, the
first arm to run is that route off. A fix on the engine's side would hold a reference while an item is queued.
