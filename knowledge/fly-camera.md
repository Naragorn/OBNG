# The free fly camera (console `tfc`)

Found while making OBVR's sticks steer it (OBVR, 2026-10-31,
`src/game/FlyCam.h`, `src/vr/FlyCamLogic.h`).

## Read in Oblivion.exe 1.2.416

- The console command is **ToggleFlyCam**, short `tfc`, opcode 0x16E, help
  "Toggles the Free Fly camera (UFO cam)"; its table entry at 0x00B0C550,
  its handler 0x005013E0: `mov ecx,[0x00B333C4]` (the player),
  `call 0x006669F0`, return 1. (read, from the EXE's bytes)
- 0x006669F0 flips the byte **PlayerCharacter+0x748** (on when non-zero).
  Switched on it seeds the camera's place +0x754/+0x758/+0x75C from the
  player's position (vtable +0x174), z raised by vtable +0xEC times the float
  +0x5D4, its yaw +0x74C from rotZ (+0x28) and its pitch +0x750 from rotX
  (+0x20). First bytes `56 8B F1 80 BE 48 07 00 00 00` (push esi; mov esi,ecx;
  cmp byte [esi+748h],0). (read, `0x6669f0.c`)
- PlayerCharacter::Update (0x00671620) calls **0x006643A0** every frame while
  +0x748 is set (line 131 of `0x671620.c`), skips the player's own walking
  (line 557) and mouse turn (line 190), and skips 0x0066B710 (line 1093).
  (read)
- 0x006643A0: input from OSInputGlobals (`[[0x00B33398]+0x20]`); the mouse's
  axes 1 and 2 turn yaw and pitch (scaled by the float 0x00B14EE8; the byte
  0x00B14F38 negates the pitch - probably the invert-Y setting, derived);
  Forward/Back (controls 0/1) set local y to +-10, StrafeRight/Left (3/2) x to
  +-10, through `0x00403520(control, 0)`. z is never set: **no up or down**;
  Jump and Sneak are not read. The vector goes through the yaw/pitch matrix
  (0x007100A0 builds, 0x007101F0 applies) and is added to +0x754.. - so it
  flies along the look, pitch included, a fixed **10 units a frame**, frame
  time not counted (derived). The result is written into the scene graph's
  first child ([0x00B333CC] +0xB0[0]): translate +0x54..+0x5C, rotation +0x30
  (nine floats), then 0x00707370(0,0). (read, `0x6643a0.c`)

## Measured in the game (OBVR harness, `tools/hand-scripts/flycam.txt`)

- 2026-10-31 07:22: OBVR's stick keys, sent the way that walks the player,
  did not move the fly camera: its place stood at 255.4 -1662.6 -140.5 at
  every mark. Why the engine's step did not take them was not traced (the
  route OBVR injects through may not be the one 0x00403520 reads - unverified).
- 2026-10-31 07:34: writing +0x754.. and +0x74C at Present moves and turns it;
  the eye pictures follow (lifted 331 units above a ceiling, turned 112
  degrees). OBVR's camera hook still ran every frame while it was on - the
  skip of 0x0066B710 does not take OBVR's camera pass away (measured: "camera
  pass 1" at every mark; which path still runs it was not traced).

## What a modern engine does instead

A free camera as its own camera object with its own controller (position,
yaw, pitch, a speed in units per second, vertical axes), not fields on the
player; input from an action map that every device feeds alike, so a VR or
gamepad layer needs no special case; frame-time-scaled movement. (derived;
general engine practice, no source quoted)
