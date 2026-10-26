# The mouse wheel and the player's camera

Status: knowledge, written 2026-10-25 from OBVR (`../OBVR`,
`src/game/MenuPausePolicy.h` PlayerControlsReadAxis, `src/game/MenuPause.cpp`,
docs/controls-spec.md 4.35).

Marks: **read** = read in Oblivion.exe 1.2.0.416 (objdump), **measured** =
measured in the game (OBVR's harness), **derived**, **unverified**.

## Where the wheel is read (read)

- 0x00403190 is the input's mouse axis read, thiscall with the axis on the
  stack (`ret 4`): 1 and 2 the mouse's x and y, 3 the wheel. Ten call sites
  in the EXE.
- The player's own input reads the wheel at 0x00671973 (`push 3; call
  0x00403190`), stores it, and in first person (`cmp byte [ebx+0x588],0` at
  0x00671AC9) a notch back calls 0x0066C580 with 0 at 0x00671AE9: the switch
  to third person. In third person the same value changes the zoom at
  0x00B3BB24. (read)
- The menus read the same axis at 0x005823E6. (read)
- The player's input runs only when the update step's IsMenuMode at
  0x0040DC61 answers "no menu"; in vanilla it never runs under a menu, so the
  wheel is the menu's alone. (read)

## What that means (measured)

A mod that lets the player's controls run under a menu (OBVR: walking under
a container opened by reaching) hands the menu's scroll wheel to the camera
too: the first notch back switched to third person
(reach-walk-wheel.txt, 2026-10-25, FAIL before the fix). The fix answers 0
for axis 3 at that one call site while in menu mode.
