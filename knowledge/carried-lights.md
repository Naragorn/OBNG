# Carried lights: why a dropped torch is dark, and what decides the torch in the left hand

Status: knowledge, written 2026-10-26 from OBVR (`../OBVR`: `src/game/TorchFire.*`,
`src/game/TorchKeepLogic.h`, docs/free-weapons-spec.md 5.7, the torch-*.txt harness scenarios).

Marks:
- **read**: read in Oblivion.exe 1.2.0.416 (objdump).
- **measured**: measured in the game with OBVR's hand-script harness.
- **derived**: follows from what was read or measured.
- **unverified**: not checked.

## A light reference's fire (read)

The light record's flags sit at TESObjectLIGH +0x7C:

| Flag | Meaning |
|---|---|
| 0x1 | dynamic |
| 0x2 | carried |
| 0x4 | negative |
| 0x8 | flicker |
| 0x20 | off by default |

The values match xOBSE's GameForms.h, consulted through DeepWiki.

- **Flag 0x80, "fire out".** It is the reference's form flag 0x80, set and cleared by
  0x0046AB60 (a thiscall taking a bool). A new reference of a light takes it from the light's
  "off by default" bit (0x0044A968).
- **Flames.** The 3D's set-up reads the flag at 0x004E41DA. If it is set, 0x004DE1C0 takes
  the flames off and hides "FlameCap". If it is clear, 0x004E26F0 puts them on: each
  "FlameNode<n>" gets the model that LookupFormByID finds for the digit, and "FlameCap" is
  shown.
- **The light.** 0x004B1600 (TESObjectLIGH, thiscall(ref, node)) makes a NiPointLight under
  "AttachLight" and hands it to the scene. For an "off by default" light it does so only when
  the reference's vtable +0x190 says yes. An actor's does; a plain reference's answers false
  (0x0069D990). In that case it also sets the fire out again.

## The torch on the ground (measured)

- **As vanilla drops it.** Torch02 has flags 0x2B, so it is "off by default". The dropped
  reference has fire out 1, no flame, the cap hidden and nothing under AttachLight. That is the
  "lightless log" the drop-lit-torch mods work around.
- **Lit through the engine's own calls.** Clear the flag, put the flames on, and call 0x004B1600
  with "off by default" cleared for that one call. The torch then burns and holds a
  NiPointLight. Its 3D, built anew after the cell buffer was purged, comes back with flames and
  without the light; the light can be added again.
- **The flag is saved.** A save of the burning torch, loaded in a freshly started game, gives
  the torch with fire out 0 and its flames, and without the light. So the reference's own flag
  can mark which torches are to burn, with no save data of a plugin's.
- **The crash.** Flames put on a 3D that is already in the scene draw without their render
  state, and NiDX9RenderState faults on the flame's NiTriShape. Flames put on in the frame the
  engine made the 3D draw well. Derived: the engine sets the render state up when it hangs that
  3D into the cell.

## The torch and the left hand (read)

0x005E7230 is an Actor method (esi = the actor) that decides what the left hand holds.

- **What it reads.** The torch (process vtable +0xF0), the shield (+0xF8) and the weapon (+0xEC).
- **Left hand needed.** The weapon types 1, 3, 4 and 5 (the two-handed blade and blunt, the
  staff, the bow) set this flag at 0x005E7345. With it set and the weapon out, the worn torch
  is hidden.
- **How it hides.** It takes the 3D off (0x004DCAB0) and clears the worn list from the
  process's copy (0x004853B0). The torch is not unequipped.
- **Where.** Two paths: 0x005E73E6 (a weapon or a torch equipped) and 0x005E76C6 (the refresh
  that the draw calls). Sheathing brings the torch back through the same function.
- **A one-hander.** The torch stays; it is hidden only when a shield is worn too.

## For OBNG (derived)

- **One question per light, not three.** Whether a light burns should be one state on the
  reference, set by whoever holds or drops it. Here it is three answers in three places: the
  record's "off by default", the reference's fire flag, and whether the holder is an actor.
- **One rule for both hands.** What the off hand holds while a weapon is drawn should come from
  the hands' actual use. Today it comes from a list of weapon types.
