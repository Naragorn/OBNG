# Particle systems: where they update, and world-space births

Status: knowledge, written 2026-10-25 from OBVR (`../OBVR`,
`src/game/ParticlesAfterPins.h`, docs/controls-spec.md, the torch entry).
OBVR moves the update of first-person particle systems as an engine detour;
nothing of it is built in OBNG.

Marks: **read** = read in Oblivion.exe 1.2.0.416 (objdump, RTTI vtables),
**measured** = measured in the game (OBVR's hand-script harness),
**derived** = follows from the two, **unverified** = not checked.

## The classes and their slots (read)

Vtables found from the MSVC RTTI type names (`.?AVNiParticleSystem@@` and
the complete object locators that point at them):

| Class | Vtable | Slot | Function |
|---|---|---|---|
| NiParticleSystem | 0x00A85B0C | 7, LoadBinary | 0x00749B70 |
| NiParticleSystem | | 29, UpdateWorldData | 0x00749310 |
| NiParticleSystem | | 30, UpdateWorldBound | 0x00749550 |
| NiParticleSystem | | 38, Update(time) | 0x007493D0 |
| NiParticleSystem | | 39, the update proper | 0x007498F0 |
| NiPSysUpdateCtlr | 0x00A8669C | 21, Update(time), at 0x00A866F0 | 0x00753BE0 |
| NiPSysUpdateCtlr | | 28, TargetIsRequiredType | 0x00753D30 |
| NiPSysEmitterCtlr | 0x00A862C4 | 21, Update(time), at 0x00A86318 | 0x00750260 |
| NiTimeController | 0x00A7EB24 | 21 and 28 | 0x00982D85 (purecall) |

- **World space** is a bool at NiParticleSystem+0xC0, read from the NIF by
  LoadBinary (`mov [esi+0xC0],cl` at 0x00749BAC). With it set,
  UpdateWorldData (0x00749310) writes the identity into the system's world
  transform and UpdateWorldBound (0x00749550) takes the data's bound as the
  world bound: the particles' positions are world positions. (read)
- **The update** (slot 38, 0x007493D0) can hand the work to a task when the
  byte at 0x00B4081C is set; nothing in the EXE writes it, so it runs
  synchronously through slot 39, which walks the modifier list at +0xC8 and
  calls each modifier's update (vtable +0x4C) with the data. (read; the byte
  staying 0 at run time is unverified)
- **Births** are not the update controller's: NiPSysEmitterCtlr, a time
  controller of its own on the same system, counts the particles due and has
  the emitter place them at the emitter object's world transform of that
  moment. Holding back NiPSysUpdateCtlr alone left the births where they were.
  (read for the classes; measured for the effect, below)
- NiPSysUpdateCtlr::Update takes its target at +0x30 and calls the system's
  slot 38 (`call [eax+0x98]`) with the scaled time. (read)

## What that means for anything that moves a model after the update (measured)

The engine updates a model's controllers, particle births included, in its
own downward update. Whatever moves the model after that - in OBVR the
first-person arms placed at the VR controllers - leaves the world-space
particles born at the old place. A held torch (its "FireParticles" and
"Sparks" are world space) showed a second fire where the game's own torch
idle holds the torch: the emitter at 230.9 -1633.8 -129.3 before the move,
253.9 -1614.5 -159.5 after, the particles round the first
(torch-flame-ghost.txt, 2026-10-25). Moving both controllers' updates to
after the move, with their own time, put the particles round the second.

## How a modern engine does it (sources)

- Unreal's Niagara and Unity's VFX Graph sample the emitter's transform at
  simulation time, which runs after animation and gameplay transforms in the
  frame (tick groups: Unreal's "PostUpdateWork" for effects attached to
  animated meshes). (unverified here, from general knowledge of their tick
  ordering; to be checked against their docs before relying on it)
