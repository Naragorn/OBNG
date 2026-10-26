# Weapon equip and draw: when the weapon reaches the hand

Status: knowledge, written 2026-10-25 (probe and timings added the same day) from OBVR (`../OBVR`,
`src/game/WeaponTimeline.h`, `src/game/WeaponDrawSpeed.h`,
docs/free-weapons-spec.md 5.6). Nothing of it is built in OBNG.

Marks: **read** = read in Oblivion.exe 1.2.0.416 or the game's own files,
**measured** = measured in the game (OBVR's hand-script harness),
**derived** = follows from the two, **unverified** = not checked.

## The pipeline (read in OBVR's code, the engine parts as cited)

A weapon equipped while sheathed is first on the sheath. Drawing plays the
Equip animation group (group code 17; Unequip is 18, at TESAnimGroup +0x08),
and the weapon changes from the sheath to the hand at the group's **Attach**
text key; sheathing moves it back at **Detach**. The player's action stays
Equip or Unequip until the group's sequence is inactive again
(OBVR `WeaponDrawSpeed.h`, measured 2026-09-28).

The key names sit in Oblivion.exe's .rdata as "Attach" (0x00A527A8) and
"Detach" (0x00A527A0), referenced from a pointer table at 0x00B10928 and
0x00B1093C (read). The key handler TESAnimGroup 0x0051AF70 handles the sound
kinds 0 to 15 - 9 and 10 play the Equip and Unequip sounds, 14 and 15 the
horse's FSTHorseSoft and FSTHorseRide, 11 and 12 read a name or a number from
the key's own text - and no case of it reparents a weapon (read, Ghidra's
pseudo-code of 0x0051AF70). Where Attach is acted on was not found
(unverified).

## The authored times (read, pyffi on Oblivion - Meshes.bsa, 2026-10-25)

First-person files, `meshes/characters/_1stperson/`:

| File | Attach | Length | Other keys |
|---|---|---|---|
| onehandequip.kf | 0.300 s | 0.633 s | Enum: Equip at 0.300 |
| twohandequip.kf | 0.367 s | 0.933 s | Enum: Equip at 0.367 |
| staffequip.kf | 0.367 s | 0.933 s | |
| bowequip.kf | 0.500 s | 0.800 s | Enum: Equip at 0.500 |
| handtohandequip.kf | 0.100 s | 0.200 s | Blend: 6 at 0.033, Enum: Equip at 0.100 |

| File | Detach | Length | Other keys |
|---|---|---|---|
| onehandunequip.kf | 0.467 s | 0.867 s | Enum: Unequip at 0.200 |
| twohandunequip.kf | 0.267 s | 0.733 s | Enum: Unequip at 0.233 |

So at the game's speed a one-hander is out of sight between the sheath and
the hand for 0.3 s of animation plus the blend in (0.25 s for the fists,
measured 2026-09-28) - the draw is a wait by design (derived).

## For a VR or physics-first engine (derived)

A hand that takes a weapon from the world wants it in the hand the frame it
is taken. The engine's way - into the inventory, equip to the sheath, draw by
animation, attach at a key - costs about five to eight frames even with the
animation's time run ten times faster (OBVR: the Attach in the draw's third
frame at 0.12 s of animation a frame, measured in weapon-pull.txt
2026-10-25; the other frames are the equip shown and the ready click,
derived). OBNG should let an equip put the weapon straight on the hand's
node when the hand already holds it, and keep the draw animation for the
keyboard's ready key only.

## Equipping with the weapon out (measured 2026-10-25, OBVR harness)

- The fists out by the game's own draw, then a sword equipped by the
  console: the sword hangs on the hand in the same frame and no Equip group
  plays (weapon-equip-drawn-probe.txt, `[Debug] WeaponNodeTrace`).
- The weapon-out flag set by hand (HighProcess +0x114) with nothing drawn,
  then the equip: the engine plays the Equip group by itself at once, and
  the weapon still waits for the Attach key - 5 frames instead of 7. So the
  flag is not the whole "out" state; the fists' drawn idle is part of it
  (derived).
- The flag's accessors (read in the binary): HighProcess vtable 0x00A71814,
  MiddleHighProcess 0x00A72684; slot 0xBF 0x0064AD10 `mov al,[ecx+0x114];
  ret`, slot 0xC0 0x0064AD20 `mov al,[esp+4]; mov [ecx+0x114],al; ret 4`.
  xOBSE's numbering of the two is one lower.
- A sequence's text keys (read in 0x0051AF70 and xOBSE NiNodes.h):
  NiControllerSequence +0x20 is its NiTextKeyExtraData, which holds the key
  count at +0x0C and the keys at +0x10, eight bytes each - the time, then the
  text pointer at +4. OBVR moves a taken weapon's draw on to its Attach key
  in its first playing frame by this, and the weapon is on the hand 3 frames
  after the take (39 to 44 ms at 75 Hz, one-hander, claymore and bow alike).

## The release (measured 2026-10-25)

Unequip and drop in one frame (UnequipItem, then the player's RemoveItem
with the drop flag and a position): the weapon is off the hand's bone in
that frame and the dropped reference's model is loaded the next, its Havok
body with it - one frame with no weapon anywhere. A sheathe by the Unequip
group leaves the hand only at the Detach key: 9 frames at OBVR's ten-fold
draw speed.
