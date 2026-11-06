# Water reflections

Status: knowledge, written 2026-11-02 from OBVR's water work (the stable
capture, render/WaterReflectionHook.cpp; the reflection map's size,
render/WaterReflectionMap.h). Read in the decompile export
(`re/export/rea-all/functions/0x49bef0.c`, `0x49cbf0.c`, `0x49d7b0.c`,
`0x7c0d10.c`, `0x7c23c0.c`, `0x7c2420.c`) and in the EXE's bytes; the names
are proposals.

Marks: **read** = seen in Oblivion.exe 1.2.0.416, **measured** = seen in the
running game, **derived** = follows from those, **unverified** = not checked.

## The reflection map is 256 x 256 (read)

`WaterManager::RenderReflections` 0x0049BEF0 (`__thiscall(WaterManager*,
NiCamera*, ShadowSceneNode*)`; the name from OBVR's GameAddresses.h and TES
Reloaded) runs only with `bUseWaterReflections` (0x00B07050) and the byte
0x00B42F3E set. On its first run it makes the manager's camera (slot +0x00,
through 0x00401F00 / 0x0070D590) and, when slot +0x04 (the reflection map) is
empty, calls the renderer's create-by-size routine:

    0x0049BFA8  6A 00            push 0
    0x0049BFAA  6A 00            push 0
    0x0049BFAC  6A 00            push 0
    0x0049BFAE  68 00 01 00 00   push 0x100        ; the size
    0x0049BFB3  50               push eax          ; the renderer 0x00B43104
    0x0049BFB4  E8 ..            call 0x007C2420

0x007C2420 `(renderer, size, format, usage, x)` is `0x007C1B50(renderer,
size, size, format, usage, x)` - a square rendered texture, width = height =
256. TES Reloaded writes its `ReflectionMapSize` setting to 0x0049BFAF
(Hooks.cpp, "Constructor") - the second source for the site. It also writes
0x007C1045, 0x007C104F, 0x007C10F9 and 0x007C1103 ("RenderedSurface"): the
entries 7 and 0xE of the size table 0x007C0D10 (kind -> width, height,
format, usage; the create-by-kind 0x007C23C0 uses it), both 256 x 256 - kind
7 is made by 0x007DE8A0 into slots +0x100/+0x104/+0x10C of its object, kind
0xE by 0x004826F0 into +0x24; what those textures are is **unverified**.

The map is small for a reason of 2006: it is sampled through the water's
projective matrix with the waves' per-pixel offset, so it is blurred on the
water anyway. What it costs today (derived): at 1920 pixels over 75 degrees
of view a texel of it is six screen pixels, in a headset (2568 across 105
degrees, the capture 1.1 times the eyes' width) ten. The map is rendered
anew every frame from the mirrored camera, so each movement of the camera
re-quantises the reflected edges to the map's grid - a jump of up to a texel
on every move and none at rest. OBVR measures this as the water's reflections
"resetting" under a turning head (OBVR docs/controls-spec.md, "Open bug,
2026-11-01"), and the Nexus threads on reflections that change as the player
moves have the same shape. OBVR's `[Render] WaterReflectionMapSize` replaces
the immediate; TES Reloaded has done the same for years.

## What the reflection pass does (read)

In order, 0x0049BEF0:

- The water height from slot +0x44, or from 0x004CACE0 when 0x0065E5E0 says
  so (a cell's own water height); the camera mirrored about the plane z =
  height (the rotation built with 0x00716E00 / 0x0070FCC0 / 0x00710250 from
  the camera's rows at +0x64..+0x94, the position at +0x88 reflected) and
  written into the manager's camera at +0x30 (rotation) and +0x54 (position),
  the camera's +0x60 taken from |+0x94|.
- 0x007D6FE0 / 0x007D7280(7, ...) - a shader pass selection (name
  unverified); the sky (0x00B3F928) told to update when its bytes +0x200 or
  +0x204 are 1 and +0x20C is 1 (vtable +0x144).
- Nodes hidden for the pass by setting bit 0 of their flags at +0x18 (NiAVObject
  APP_CULLED): the two at 0x00B35230 and 0x00B35234, the one 0x00660110
  returns (the player's first-person node, derived from OBVR's use of that
  getter), 0x00B36094, and the node of every loaded cell in the outer ring
  of the grid - cells with x or y below `n` or at or past `uGridsToLoad - n`,
  with n = ceil(uGridsToLoad / the setting at 0x00B070E0) (0x00B06A2C is
  uGridsToLoad; the divisor's INI name is unverified; the cell from
  0x00482150(x, y), its node from 0x004CA790). All of them restored after
  the pass. TES Reloaded skips both ring loops for speed.
- The view-projection of the mirrored camera (the matrices at renderer
  +0x980 and +0x9C0 multiplied, inverted, transposed) carries the clip plane
  (0, 0, 1, -height) into clip space (D3DXPlaneTransform), render state 0x98
  (D3DRS_CLIPPLANEENABLE) set to 1 for the pass and 0 after; 0x004EA010(0,
  0.6) before and 0x004EA010(previous) after - a global flag byte 0x00B09AE5
  with two floats 0x00B2C718/0x00B2C71C swapped between 1 and 0 (what it
  gates is unverified; the 0.6 is dropped by the callee as decompiled).
- The scene rendered into the map through the renderer (vtable +0x4C of the
  object 0x007B7650 returns, the camera at +0xDC) with 0x00483CD0(0)
  before and 0x00483CD0() after.
- At the end the map is handed to the water shader singleton 0x00B45DCC at
  +0x108 (an NiPointer assign).

What the pass leaves out, per form type, is `TESWaterCullingProcess`
0x0049CBF0 (`__thiscall(process, NiAVObject*)`): for an object whose
extra-data list carries the marker 0x00B35ACC, the referenced form's type
(vtable +0x170, byte +0x04) is looked up: ACTI, CONT, DOOR, STAT (0x12, 0x17,
0x18, 0x1C) are dropped unless the byte 0x00B07078 is set
(bUseWaterReflectionsStatics - derived from the type set); APPA, ARMO, BOOK,
CLOT, INGR, MISC, WEAP, AMMO, SLGM, KEYM, ALCH, SGST unless 0x00B07080
(Misc); TREE, FLOR unless 0x00B07070 (Trees); NPC_, CREA unless 0x00B07068
(Actors). Everything else goes on to NiCullingProcess::Cull 0x0070DFB0
(knowledge/culling-and-draw-setup.md) unless 0x00B3521C is clear and the
plane test 0x0047DA70 on the process +0x90 says 2.

## The surface pass (read, in part)

`WaterSurfacePass` 0x0049D7B0 (`__thiscall(WaterManager*, NiCamera*)`)
builds the manager's other textures on demand: the height map at slot +0x08
256 or 128 a side by `bUseWaterHiRes` (0x00B07058; format 6, usage 0x72),
the displacement maps (+0x0C, +0x10) 256 a side, through the same
0x007C2420; the wave frames are rendered by 0x007B4900 with the shader of
kind 0x13/0x14 (0x007B4290). The reflection's share is only the final hand-over
through 0x004992C0 when the shader singleton exists. Not read further.

## Measured (OBVR, 2026-11-02)

OBVR's harness scenarios `water-turn-jitter.txt` (the map at 1024) and
`water-turn-jitter-engine.txt` (the engine's 256) took the right eye at
every frame while a synthetic head turned a tenth of a degree a frame at
the rocky north shore of Lake Rumare, and read the far shore's reflection
frame against frame (OBVR docs/controls-spec.md, "Measured and fixed,
2026-11-02"):

- The map's size did not change the reflection's behaviour under the turn:
  with 256 and with 1024 the reflection stood for four frames and jumped on
  the fifth, at the same frames. The jumps went with the player's position:
  OBVR's room follow moved the player 0.05 units every four to five frames,
  and at each such `Actor::Move` with a step (0x005EFAE0, from the walk
  update 0x0063C730 at 0x0063CB42, `dt` 0.03-0.07 s a frame, flags 2048) the
  engine re-grounded the player on that shore 0.6 to 1.0 units lower
  (measured: the feet -6.28, -6.82, -7.52, -8.17, -8.80, -9.44, -10.48 over
  thirty frames; with a zero step the feet never moved). The water's eye
  position constant (c9 of the water vertex shader, the camera's position)
  and the tile translations (c4-c6, the tile's place less the eye's) moved
  with the feet.
- **Read with it:** the engine calls the player's Move every frame from the
  walk update with the animation's step (zero while standing); a non-zero
  step on that rocky shore ends 0.6 to 1.0 units lower each time, which
  names the character controller's ground handling after a move
  (unverified which part: the proxy's ground probe, or a slide down the
  rocks).
- What the bigger map buys is the reflection's sharpness, not stability.

The water vertex shader's constants as uploaded there (read in the
running game): c0-c3 the view-projection, c4-c7 the tile's world matrix
with the translation relative to the eye, c8 = (3, 0, 0, 0), c9 = the eye's
position, c10 = (2 or 1, 1 or 0, 0, 57344) - names unverified.

## A modern engine

Planar reflections at the output's resolution or half of it, with temporal
accumulation, or screen-space reflections with a planar fallback for the
far shore; a 256 x 256 map re-rendered per frame is the 2006 trade of fill
rate for blur.

## Distant water flickers through the reflection (measured, OBVR 2026-11-06)

Nadi: far-off water reflections flicker, as the foliage did. OBVR's
harness scenarios `water-far-flicker-{capture,vanilla,noreflect}.txt` (Lake
Rumare north shore, the game's clock at its normal pace, the right eye at
every frame, thirty frames with the head still and thirty turning 0.02
degrees a frame), read by OBVR `tools/water-far-flicker.py`: per band of
screen rows the mean frame-to-frame change and the mean second difference
(|f[i+1] - 2 f[i] + f[i-1]|, grey 0..255: drift cancels in it, flicker does
not). Runs 20261106-093958, -094257, -094548.

| WaterReflectionMode | land (still) | far water | mid water | near water |
|---|---|---|---|---|
| 2 OBVR's stable capture | 0.24 | 3.49 | 2.45 | 2.21 |
| 0 the engine's own reflection | 0.23 | 3.61 | 2.53 | 1.71 |
| 1 nothing mirrored | 0.23 | 0.67 | 0.60 | 1.33 |

(the second difference, head still; turning gave the same water values.)

- **Measured:** the far water's flicker is the reflection's - five times
  more with it than without, the same with the engine's reflection and with
  OBVR's capture, and the same with the head still. The second difference
  is larger than the first there (3.5 against 2.6), so the far pixels jump
  back and forth rather than drift.
- **Derived:** with the head still only the waves move, so the waves'
  per-pixel offset of the reflection lookup makes it: far off one pixel
  covers many wave texels, and the offset it takes is a different one each
  frame, so it reads a different spot of the reflected shore each frame.
  The waves' own shading is calm far off (the mode-1 run).
- **Unverified:** whether the wave textures have mip levels (the surface
  pass renders them; a render target has none unless made with them), and
  where in the water pixel shader the offset is applied. The fix this points
  at is that offset faded with distance (or with the wave texture's screen
  footprint), not more samples.
