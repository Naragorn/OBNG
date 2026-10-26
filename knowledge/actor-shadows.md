# Actor shadows

Status: knowledge, written 2026-10-26 from OBVR's shadow sharing
(`../OBVR`, src/render/ShadowShare*.h/.cpp, commit f53cf6d) and the
OblivionWalkers captures. Nothing of this is built in OBNG.

## Where Oblivion draws them (read, Oblivion.exe 1.2.0.416, REA/Ghidra 12.1.4)

- `0x004073D0` - the actor shadows of a world render; cdecl, no arguments,
  plain `ret`. Called once from the scene render `0x0040C830`.
- It reads `iActorShadowCountInt` (value at `0x00B06F04`) or
  `iActorShadowCountExt` (`0x00B06EFC`) - which one by the current cell's
  exterior flag (`[0x00B333A0] + 0x34`) - and hands the count to
  `0x007C2710`.
- Casters: the player first (if he casts one), then the actors of the process
  manager's list (`0x006747C0` on `0x00B3BD00`), each passing a set of checks
  (3D there `vtable+0x154`, `0x005EA5E0`, not knocked down/dead states via
  `vtable+0x18C`, `0x005E9670`, an actor value `0x2F` zero), up to the count.
- Drawing: a loop over the shadow scene's lights (`0x007C5AB0` /
  `0x007C5B50`); per light `0x007D2280` places it at the caster's node
  (`+0x130`), `0x007D6390` sets the camera, and a light whose fade
  (`+0xD8`) is at least 0.05 is drawn: `0x007D5ED0`, `0x007D6900` on the
  scene root `[0x00B333CC]` (and on the caster itself when
  `bActorSelfShadowing`, byte `0x00B06F0C`), `0x007D6A40`. The
  first-person node and a few others are hidden for it (app-culled flag,
  `NiAVObject +0x18` bit 0) and restored after.
- At its end, exteriors only: the canopy shadow's timer (`0x00542EA0`
  returns the object, `0x00543A00(frame seconds x 0x00B06530)`) - it
  accumulates time and re-renders the canopy shadow when it runs out.

## Measured (OblivionWalkers perf-walkers-in-view.txt, Linux, 75 Hz, OBVR dual pass)

- Eighteen Walkers standing in view, `iActorShadowCountInt` 0 against 10:
  3.73 against 5.15 ms a scene pass - ten casters 1.4 ms a pass, 2.8 ms a
  stereo frame; frames over 20 ms 20.8 % against 75.4 %.

- Shared between the eyes (OBVR `ShareActorShadows`, the same build with
  and without, ten casters, eighteen Walkers standing): 3.85 against 3.35 ms
  a scene pass, frames over 20 ms 6.7 % against 1.1 %; the bone lock's rows
  to off-screen (shadow) targets 1926 against 1070 a log window; both eyes'
  dumps show the same shadows (OblivionWalkers
  perf-walkers-shadows-shared(-eyes).txt, 15:29 to 15:38).

## Derived

- A shadow map is the caster seen from its light: it does not depend on the
  eye. Only the receivers do, and they sample the maps inside each eye's own
  scene pass. So a stereo renderer draws the maps once for both eyes; OBVR's
  `ShareActorShadows` skips `0x004073D0` in the second eye when the first
  eye ran it in the same world render.
- Running `0x004073D0` twice a frame also runs the canopy shadow's timer
  twice: under OBVR's dual pass the canopy re-renders twice as often as
  flat. Unmeasured.

## What a new engine does instead

- Shadow maps per light are rendered once per frame and shared by every
  view: OpenMW-VR's shared shadow maps for both eyes
  (openmw-vr.readthedocs.io, stereo settings), FNVVR Reborn's
  `d3d9.vrShareShadows` for New Vegas (github.com/df4l/FNVVR,
  `src/d3d9/vr/vr_render_passes.h`: drawn for the left eye with the camera
  widened to both eyes' view). With multiview (Vulkan) the receivers too are
  drawn once for both eyes.

## Sources

- REA over Oblivion.exe 1.2.0.416 (sha256 a8f31384...), 2026-10-26.
- ../OblivionWalkers/harness/perf-walkers-in-view.txt, runs 2026-10-26
  15:02 and 15:06.
