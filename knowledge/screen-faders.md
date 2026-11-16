# The screen faders: two black ones and the letterbox

Found while turning the engine's fades into fades of the whole VR view (OBVR,
2026-10-27, `src/game/ScreenFaders.h`, `src/vr/ScreenFade.h`) and while fixing
a view that went black through a scripted scene (OBVR, 2026-11-15).

Marks: **read** (in the EXE or a file), **measured** (in the game),
**derived**, **unverified**.

## Read in Oblivion.exe 1.2.416

- A static table of three records, 0x18 bytes each, at **0x00B12DC0**; the
  loop in **0x00572F60** steps ids 0..2 every frame. (read)
- A record: +0x00 a flag (1, 1, 0 in the file's data), **+0x04 the texture's
  name**, +0x08 on (fading in) or off, +0x0C its seconds, +0x10 its node
  (none: no fader), +0x14 its opacity 0..1, set as the material's alpha by
  0x004A2A90. (read)
- The texture names, read from the file's data through the pointers at +0x04
  (0x00A68890, 0x00A68884, 0x00A68884): fader 0 **LetterBox.dds**, faders 1
  and 2 **Black.dds**. (read, 2026-11-15)
- The nodes hang under "FaderNode Below Menus" / "Above Menus", built by
  0x005730B0 under the menu root: the faders are drawn in the 2D pass. (read)
- **0x00663070**(1) starts fader 0 over 1.0 s; with 0 it ends it through
  0x00572EC0 (read in the decompile, `re/export/rea-all/functions/0x663070.c`).
  Its caller is not read in the EXE; measured 2026-11-16 (OBVR harness
  letterbox-no-fade): the console's **DisablePlayerControls** turns fader 0 on
  (opacity 1.00 within 3 s) and EnablePlayerControls off again - the bars come
  with the player's controls taken away.
- Fader 2 is the one the main loop asks about after a load (0x0040D50F...).
  (read)

## Measured in the game (OBVR's logs)

- After a load fader 2 goes from 1.00 to 0 within about a second. (measured
  2026-10-27, OBVR's hud-on-hands-spec)
- At a new game, as the guards walk into the cell, fader 0 goes to 1.00 and
  stays there for the scene: "the faders at 1.00 0.00 0.00" (OBVR.log,
  2026-11-15 22:06 session, lines 5841-5884). On a monitor that is the two
  bars; taken as a fade's opacity it is a black view. The same was seen on
  2026-11-11 at the cell's window as the Emperor came. (measured)

## For a modern engine (derived)

- A fade and a letterbox are two things that happen to share a table here:
  one is an opacity of the whole picture, the other a mask whose texture
  holds the picture's shape. Keep them apart - a post-process fade factor,
  and a cinematic-bars state the presentation layer may ignore.
- In VR neither belongs in the 2D layer: the fade goes to the compositor (or
  the last pass of each eye), and bars are dropped - on a head-locked panel
  they are two black strips in front of the eyes.
