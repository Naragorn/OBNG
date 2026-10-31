# The player's movement pace: walk, run, and the gamepad's factor

How Oblivion.exe 1.2.416 turns the player's input into a walk or a run and how
fast the legs and the step go. Found for OBVR's analog walk (2026-10-31).

## Read in the EXE

- **Speeds.** Actor walk speed 0x005E3590 and run speed 0x005E3750: thiscall,
  no arguments, float in ST0, plain `ret`; both begin
  `83 EC 10 56 8B F1 8B 06` and return 0 when vtable +0x25C is true (meaning
  unverified). GetCurrentSpeed 0x005E65B0 picks by the movement flags: 0x200
  run, 0x800 swim, 0x2000 fly, else walk.
  - Walk (0x00547C00): `fMoveCharWalkMin 90 + Speed x 0.01 x (fMoveCharWalkMax
    130 - Min)`, times `(1 - w x fMoveEncumEffect 0.4)` (no weapon drawn:
    `fMoveEncumEffectNoWea 0.3` and `x fMoveNoWeaponMult 1.2`), times
    `fMoveSneakMult 0.75` sneaking (0x400 without 0x800). Creatures use
    fMoveCreatureWalkMin/Max.
  - Run (0x00547D00): walk x `(fMoveRunMult 3.0 + Athletics x 0.01 x
    fMoveRunAthleticsMult 1.0)`.
- **The step is root motion.** UpdateMovement 0x0063C730 takes the step from
  the movement animation's root motion (0x004723A0, ActorAnimData +0xC..+0x14),
  then calls Move (vtable +0x1B4) with it. The animation's rate (+0xBC) is set
  in 0x005FCAB0 (0x005FE03B..0x005FE06C) as GetSpeed / the animation's authored
  speed x a factor. There is no backward or sideways multiplier (the strings
  fMoveBackwardMult / fMoveSideMult are not in the EXE).
- **The gamepad's factor 0x00B14E58** (.data, 1.0 in the file). The player's
  handler 0x00671620 writes `|stick| x 0.02`, at most 1, only when a gamepad
  stick is deflected (0x00672144, 0x00672185), and hands it to 0x005FCAB0
  (0x00672E0D). So it scales the legs and the step together.
- **Always Run** is the byte PlayerCharacter +0x58B (the control toggles it at
  0x00671E82). Off: the Run control held sets 0x200. On: Run not held runs,
  Run held walks.
- Per frame without a horse: SetMovementFlags (0x00631B50) at 0x006725A5, the
  animation update 0x005FCAB0 at 0x00672E0D, then UpdateMovement at 0x00672EFD.

## Measured in the game (OBVR harness analog-walk, 2026-10-31 13:50)

- The harness character: walk 114.5, run 355.0 units a second (the getters'
  values, logged).
- The factor written as 0.63 with the walk: 71 units in the first second
  (72 expected); 1.0 with the run: 346 in the first second.
- The octant's step turned by 15 degrees inside Move still walked through the
  character controller: 29.9 degrees measured for 30 asked.

## What a modern engine does instead

An analog locomotion input (a 0..1 magnitude and a direction) blends the
walk and run animations by speed (a 1D blend space) and drives the
controller from the wanted velocity, with the animation's rate matched to
it; root motion is optional. Oblivion has the factor, but only the gamepad
feeds it, and only one animation plays per gait.

## The factor is clamped to 1 every frame (read and measured 2026-10-31)

- Read: in 0x00671620 the clamp `if (1.0 < factor) factor = 1.0` on 0x00B14E58
  sits outside the gamepad's branch, so it runs every frame; a value over 1
  written from outside never reaches the animation update.
- Measured: 1.40 written each frame, the run stayed at 355 units a second.
- The call that hands it on: `fld [0x00B14E58]` at 0x00672E04, `fstp [esp]`,
  `call 0x005FCAB0` at 0x00672E0D (thiscall, the pace and the turn factor,
  `ret 8` at 0x005FE2DF). Taking that call and passing 1.40 gave 494 units a
  second (x1.42) with the run's legs.
