# The Tab menus' open and close: what the frame pays for

Status: knowledge, written 2026-11-20 (backlog 12e, from OBVR's "the opening hitch", ../OBVR
docs/inventory-tablet-spec.md section 13). Marks: **read** = read in Oblivion.exe 1.2.0.416, **measured** = measured
in the game, **derived** = follows from the two, **unverified** = not checked.

Everything measured here: Linux, Proton, no SteamVR (the stand-in Dream Air at 72 Hz, a frame 13.9 ms), the plain
Linux game with OBVR's harness save (a small inventory, an idle world at timescale 0), OBNG.dll 4e8c2903 with the
routes as played, OBVR.dll efb31c39, `[Render] LatePoseWait=1` and `[Tablet] Enabled=1` as in Nadi's game unless
said. Scenario: OBVR's `tablet-open-hitch-late.txt` (the left B opens and closes the menus five times); measuring
INIs `tools/hand-scripts/obng-measure-menu-open.ini` (the spike watch on the interface's update) and
`-render.ini` (on the 2D render). Runs `artifacts/shadow/tablet-open-hitch-late/20261120-203805`, `-204037`,
`-204517`, `-204750`, and `obng-menu-open-quad-late/20261120-205021` (the tablet off).

## The path (read)

- **The open is 0x0057CAC0**, called from the interface's update 0x005821F0: with no menu made yet 0x0057E150 makes
  all four (stats 0x005DCB70, inventory 0x005AB5A0, magic 0x005B3760, map 0x005BB6C0, each `Tile::ReadXML`
  0x00590420 on its XML file) - once a session; then the sound (0x0057DE50 with 0x10, "UIInventoryOpen"), the last
  shown menu's show (0x0057C7C0 inventory, 0x0057A480 stats, 0x0057C5F0 magic, 0x0057C420 map; the inventory's
  refills its list each time, 0x0057A3B0 -> 0x005AADC0) and 0x0057D5B0, which gets the figure and hangs it into the
  interface's scene.
- **The close is 0x0057CC00**: the four hides, and 0x0057ECB0, which takes the figure out and destroys it
  (0x00668D00 with 0).
- **The figure is rebuilt at every open.** 0x00668D00 (the player's "PlayerInventoryPC", knowledge/menu-paperdoll.md)
  with 0 destroys the figure and returns; with 1 it builds it when there is none: a copy of the player's biped, the
  FaceGen nodes, an animation data of its own with a directory listing of its KF files (0x00434850 -> 0x00431970)
  and the animation set up 0x00475D80. The pseudocode reads as "destroy, then rebuild" for 0; the instructions say
  otherwise (`cmp byte ptr [esp+0x2AC], 0; je 0x006695FB` jumps to the destroy block and out).
- **A sound that is not an MP3 is read from its file every time it plays**: 0x0057DE50 -> the sound by path
  0x006AC610 -> for a folder the pick 0x006A9CD0 (a listing of `<folder>*.wav` through 0x00431970, one file by the
  game's random number 0x0047DF80), then the WAV reader 0x006AABD0 (mmioOpen with the game's procedure 0x006AAAA0,
  which opens the file through 0x00431130 and reads it through 0x0042C8E0; a DirectSound buffer made and filled
  byte by byte). `bUseSoundDebugInfo` 0x00B161C0 and `bSoundEnabled` 0x00B16178 are the only settings in the way;
  there is no cache.
- The 2D render is 0x0057F170: the tiles' update 0x005903E0 (-> 0x0058FBA0), then the menu scene's draw
  (0x0070C0B0, 0x0070E0A0). A tile value's change runs the trait network 0x0058BEE0 (0x0058CA00 sets a float,
  0x0058CEB0 by id).

## What an open and a close cost (measured)

The interface's update 0x005821F0, normally 0.01 ms:

| | ms | of it |
|---|---:|---|
| an open | 2.4 - 5.1 | the sound 0x006AC610 0.5 - 2.7 (the file's reads 0.3 - 2.2, the folder's listing 0.19 - 0.24); the figure 0x00668D00 1.0 - 1.2 (inside 0x0057D5B0, 1.1 - 1.3); the inventory's refill 0x005AADC0 0.7 - 1.2 (140 values set, the trait network 299 calls 1.5 - 2.0 ms across all of it) |
| a close | 1.4 - 6.6 | the sound 2.1 - 6.1 (25 - 37 reads of the file 1.9 - 5.4 ms); the figure's destroy 0.2 - 0.3 |
| the first hover after an open | 2.4 | the sound 2.3 |

- The sound is the largest part and the uneven one: the same file costs 0.3 ms one time and 5 ms another, all of
  it in the file's reads (0x0042C8E0). Why a read is slow some times is not known (the archive's lock held by the
  loader's thread is a candidate - knowledge/background-loading.md has a 13 ms wait of that kind; unverified here).
- One open's update took 14.5 ms (run `-204037`, the fourth open; the spike watch was on the render in that run,
  so its parts are not known).
- The inventory's refill grows with the inventory (derived: a tile and its values per item); Nadi's character
  carries far more than the harness save.

**The frame.** With the late pose wait the update runs before the wait and is taken up by it: the open's frame is
14.6 - 15.2 ms against 13.9 (the wait ends 0.7 ms later than usual on those frames - the stand-in's sleep, derived -
and the 2D render takes 0.9 - 1.3 ms against 0.7). The session's first open is 18.0 - 18.9 ms: its 2D render is
4.6 - 5.1 ms once (the new tiles' first update 1.4 - 1.9 ms and first draw 2.6 - 3.2 ms). Without the late wait the
whole update lands behind the wait (OBVR's captures: 17 - 28 ms frames at every open). In a world with a busy
update the late wait has less room; that is derived, not measured.

**After the open, with the tablet** (OBVR's side): one to three more frames of 15 - 21 ms 80 - 380 ms after the
open's frame - the scene pass 2.0 - 4.6 ms against 0.9 - 1.3, the engine's 2D render 1.9 - 5.2 against 0.7, in
them the tiles' update 1.6 - 4.0 ms and single draws 0x007693E0 of 1 - 3.9 ms. With the tablet off: one such frame
in two of five opens. The trigger is not found; a hover's focus change is a guess (a UI sound plays 250 ms after
the open).

## `[Route] SoundFileCache` (class A, built, checked in a slot, not deployed)

plugin/src/SoundFileCache.cpp, src/engine/sound/SoundFileCacheLogic.h, tests/SoundFileCacheLogicTest.cpp. The WAV
reader keeps its own code; the four bytes of its mmio procedure's address at 0x006AAC4A are replaced by OBNG's
procedure, which reads a file whole through the game's procedure at its first open and serves every later open
from memory (files up to 1 MiB, 24 MiB in all, the least recently played dropped first). The folder's pick
0x006A9CD0 is replaced at its entry: the listing kept by its pattern, the pick by the game's random number drawn
as the game draws it. 1 routes, 2 checks (the game's file open beside the kept bytes, every answer of the
procedure compared; every folder listed again and compared).

- **Checked** (run `artifacts/shadow/obng-menu-open-check/20261120-205752-slot3`, a --parallel slot, the DLL
  48564921 through --plugin, `obng-check-sound-cache.ini`): 16 opens, 8 of them from kept bytes, 836 answers
  compared, 0 mismatches; 10 folder listings compared, 0 mismatches.
- **Not done**: a check run with many sounds (a ride, a fight), the route's own timing against the table above,
  a long session, the deploy. Off as shipped (`Shipped()` 0); in no game's OBNG.ini.

## Levers left (not built)

- The figure kept between opens instead of destroyed at the close: 1.0 - 1.2 ms an open. It needs a rule for when
  the kept figure is stale (equipment changed while the menu was shut); class B.
- The inventory's refill: only what changed, or the tiles kept; not read further.
- The first open's 4.6 - 5.1 ms of 2D render: once a session.
