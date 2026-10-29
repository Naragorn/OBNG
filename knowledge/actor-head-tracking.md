# Actor head tracking (and the spine twist with it)

Found 2026-10-29 for OBVR (the player's visible body turned towards NPCs). Oblivion.exe
1.2.0.416, from the decompiled functions in `re/export/rea-all/functions/` and the bytes of the
EXE. **Read** = in the code or bytes; **derived** = concluded from it; **unverified** = not
measured in the running game yet.

## What the engine does (read)

- The actor update `0x00603CA0` calls the head-tracking step `0x00603500` (gates: actor+0xB0 != 3,
  vfuncs +0x198/+0x19C, `0x005E0F30`). For the player it runs on the pass with the third-person
  flag (player+0x588) set; the player's update callers (`0x00671620`, `0x0066EAF0`) call
  `0x00603CA0` twice a frame, flipping +0x588 between.
- `0x00603500` needs actor+0xC4 bits 0..2 set (else it clears the object's +0x1D5 and returns),
  picks a target every 0.2-0.4 s from nearby actors (`0x00601670`; a package of type 6 or 9 with a
  target within 100 units, 500 for the player, wins), sets it through process vfunc +0x480 and holds
  it 4-10 s. So it is not only greetings.
- The head-tracking object: HighProcess vtable `0x00A71814` slot +0xE8 = `0x0064B350`, which builds
  it once from the actor (`0x004D66B0(actor, 0)`) and caches it at the process's **+0x15C**
  (bytes `... 89 86 5C 01 00 00`); `0x005E12B0` reaches it through that slot.
- Its fields: +0x170..0x178 the look-target point (written by `0x0054C5D0`, which also sets +0x1D5);
  +0x1D5 tracking active; +0x1D4 snap now; +0x19C/+0x1A0 target yaw/pitch; +0x184/+0x188 the head's
  current yaw/pitch; +0x198 body twist allowed; **+0x1DA stops it all** - every reader
  (`0x0054B010`, `0x0054B3E0`, `0x0054B5A0`, `0x0054C6B0`, the face's expression/phoneme readers
  `0x00549640`..`0x0054ABC0`) returns early or answers zero/identity. Only writer found: the
  constructor `0x0054CAF0`, with 0. The object also carries the face animation (expressions,
  phonemes, modifiers), so +0x1DA freezes the face too.
- The spine twist: past 45 degrees of yaw, +0x1B0 = 0.75 x yaw; `0x0054B3E0` rate-limits +0x17C to
  it, clamped to 45 degrees (pitch 22.5); `0x0047CAF0(yaw)` then writes it split 30/35/35 % over a
  bone and its two parents (which bone it starts at is **unverified** - Ghidra lost ECX; derived:
  Spine2, so Spine, Spine1, Spine2). Global off switch for all actors: `DAT_00B148F4` (a setting;
  its name not read).

## What OBVR does with it

`game/PlayerHeadTrack.cpp`: while the body is shown in first person, the player's object's +0x1DA
is set each frame and cleared when the body is no longer shown. **Unverified in the game** as of
2026-10-29 (no run yet).

## How a modern engine does it (unverified - from general knowledge, not looked up)

A look-at IK layer on the animation graph (Unreal's Look At / Control Rig, Unity's Animation
Rigging Multi-Aim): per-character, weighted, with a blend weight that a gameplay system - here the
VR body - can set to 0 without touching facial animation. Oblivion ties the look-at and the face
to one object and one byte, so stopping the look stops the face.
