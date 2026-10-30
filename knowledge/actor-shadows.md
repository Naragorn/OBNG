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

## At instruction level (read 2026-10-30, `re/functions/0x004073D0.md`, held by three skeptics)

Proposed name `RenderActorShadows` (OBVR's seed `kRenderActorShadows`);
`void(void)`, plain `RET`, ECX not an input; 532 instructions, 33 callees.
In order:

1. **An enable/disable state machine** on bit 0x10 of the dword 0x00B42F40
   with the bytes 0x00B333B8 (a request) and 0x00B333F6 (a latch), gated by
   `[0x00B42F48] >= 3`: a set request disables shadows (clears the bit,
   `0x007B4280(0)->0x007C5EF0()`, `0x007C2710(renderer, 0)`), a cleared
   request with the latch set re-enables them and reserves `count` maps
   through 0x007C2710. What 0x00B42F48 and 0x00B333B8 are is unverified
   (0x00B333B8 is also the one-frame flag the frame body hands the Havok
   planner, `re/functions/0x0040D800.md`; one neutral name is still owed).
2. `0x007B7650()` on every path; then the gate: byte 0x00B42CDB set,
   level below 3 or the bit clear go straight to the timer.
3. **Caster selection**, `count` = dword 0x00B06F04 (exterior,
   `[g_TES]+0x34` set) or 0x00B06EFC (image values 4 and 2; the Int/Ext
   setting names are OBVR's, the name pointers 0x00A3EC40/0x00A3EC20 are
   not dereferenced here). With count 0: `scene->0x007C7880()` and the
   timer. The player first, once, without an upper-bound check: in first
   person only when `0x0065D650()` is non-zero; then, when `vt+0x380()` is
   non-zero, `vt+0x18C()` must not be 4, 3 or 5; `0x005E9670()` must be 0;
   the actor value 0x2F (`vt+0x284(0x2F)`, compared as a float with 0.0
   through `TEST AH,0x44 / JP`) must be exactly 0; accepted through
   `scene->0x007C6C30(player->0x00660110(0))`. Then the process list
   `0x006747C0(0x00B3BD00)`, stopping at the first node whose data is null:
   per actor, `accepted < count` first, `vt+0x154()` non-null,
   `0x005EA5E0()` true, (when `vt+0x380()` non-zero) `vt+0x18C() != 4`,
   `0x005E9670()` false, actor value 0x2F zero. **First come, first served
   in list order** - nothing here ranks casters by distance, visibility or
   screen size (derived); whether the player can appear in the list too
   (registered twice) is unverified - the loop never compares against
   0x00B333C4. The NPC check rejects only state 4 where the player also
   rejects 3 and 5.
4. **Hiding and the light walk**: the first-person node, `[[g_TES]+0x5C]+4`,
   the object at `[0x00B36094]` (unless byte 0x00B06F14) and two
   TES-managed node groups (`0x00442A30` with 0 and 3) are app-culled, the
   culling process's +0x0C set to the scene camera, then the shadow scene's
   lights (0x007C5AB0/0x007C5B50) are walked and **at most `count` of them
   drawn** - a light is counted only when drawn; a light whose
   `0x00405AD0` object is null is skipped entirely (no fade call, no hide,
   no draw); a drawn light runs 0x007D2280 (placed at the caster node
   +0x130), 0x007D6390 (camera), 0x007D5ED0, 0x007D6900 on the scene root
   `[0x00B333CC]` (g_worldSceneGraph) and, with `bActorSelfShadowing`
   (byte 0x00B06F0C), on the caster, then 0x007D6A40; otherwise its object
   is hidden.
5. **Restore**: what was hidden is un-culled, 0x007C6770 is bracketed by
   `0x00977C50(3)` / `0x00977C50(2)`, byte 0x00B0727C set back to 1.
6. **The Sky timer on every path**, early exits included:
   `Sky_GetSingleton()->0x00543A00(fSetting 0x00B06530 * frameSeconds)`
   when byte 0x00B430AD is set and a menu-mode condition allows it. So it
   reads frameSeconds (0x00B33E9C) directly; whether 0x00B430AD is zero in
   interiors (the earlier "exteriors only") is unverified. That this timer
   is the canopy shadow's rests on OBVR's reading, nothing in this export
   says "canopy".

Lever (derived): the cost is the per-light draw (step 4) times the number
of casters admitted in step 3; a rebuild of the selection (fewer, better
chosen casters) changes the count without touching the draw. Which callee
carries the measured 1.4 ms needs a per-callee timing. Worth their own
slice: 0x007C6C30 (what registering a caster does), 0x007D6900 (the draw),
0x007C2710 (the map reservation).

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
