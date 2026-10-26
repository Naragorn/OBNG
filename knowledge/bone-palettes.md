# Bone palettes: how the renderer uploads skinned bodies, and what a crowd costs

Status: knowledge, written 2026-10-26 from OBVR (`../OBVR`: `src/render/BoneRebase.h`,
`src/render/InterfaceRenderHook.cpp` HookedSetVsConstantF, docs/performance-spec.md section 7;
the Walkers session's capture `../OblivionWalkers/artifacts/hand-script/perf-walkers-in-view/20261026-141551`
and OBVR's `artifacts/hand-script/perf-walkers-fight/20261026-145000` and `-145514`). Ghidra 12.1.4
decompilation through REA. Nothing of it is built in OBNG.

Marks: **read** = read in Oblivion.exe 1.2.0.416 (Ghidra decompiler), **measured** = measured in the game
(OBVR's hand-script harness, the render sampler), **derived** = follows from the two,
**unverified** = not checked.

## The palette builder 0x007655F0 (read)

A thiscall on the renderer (`ecx` carries the renderer; `[ecx+0x8ac]` is an object whose vtable slot
+0xfc is called at the end with a flag). `param_1` is a skin instance record:

| Field | Meaning |
|---|---|
| +0x08 | the skin data; its +0x40 is the bone count, its +0x44 the per-bone bind transforms, 0x4c bytes each (a 3x3 rotation, a translation, a scale: thirteen floats, then the bound) |
| +0x14 | the bone node array (per bone a pointer; the node's +0x94 is its world scale) |
| +0x18 | the frame stamp (0x00422DE0 returns the frame counter); the palette is rebuilt once a frame |
| +0x1c / +0x20 / +0x24 | the palette buffer's capacity, pointer and mode; reallocated through 0x00401F00 / 0x00401F20 when the bone count grows or the mode changes |
| +0x28 | a 0x40-byte scratch transform, allocated on first use, filled by 0x00761BE0 |

Per bone the builder multiplies the bind transform by the bone's world transform (0x0053D7A0, the
engine's transform multiply, a thirteen-float transform), scales by the skin's world scale and
writes a 4x3 row of twelve floats - three float4 vectors, floats 3, 7 and 11 the translation -
into the palette, or packs it through 0x00761AE0 / 0x00761B60 in mode 4. The rows go to the GPU
as vertex shader constants, three float4 registers a bone, in three register classes (OBVR's
census, read from the uploads): skin shaders at c42 upward (rows on registers divisible by 3),
hair at c31 (1 mod 3), head parts - eyes, teeth, some hair - at c14 (2 mod 3). The non-uniform
scale flag (`local_e0`) goes to the renderer slot +0xfc.

## What a body costs the render thread (measured)

ImperialDungeon01, the Beyond at 75 Hz under Proton, dual pass (two world renders a frame), the
sampler on the render thread (about one sample a millisecond):

| | empty prison | 18 Walkers standing (AI off) | 18 Walkers fighting |
|---|---:|---:|---:|
| SetVertexShaderConstantF calls a pass | 2178 | 13222 | - |
| draws a pass | 261 | 1186 | - |
| bone rows a render (OBVR's count of replaced rows) | 616 | 5200 to 5700 | 5176 to 5264 |
| render thread inside the passes, samples a frame | 1.6 | 7.5 | 10.4 |
| of them Oblivion.exe | 1.0 | 3.6 | 4.6 |
| of them D3D9 (DXVK) | 0.3 | 0.9 | 1.1 |
| scene pass (OBVR's span, ms a pass) | 0.84 to 0.89 | 4.17 | 5.6 to 5.9 |

Derived: a Walker costs the engine itself about 0.14 ms a pass standing (the Oblivion.exe share
alone: 2.6 samples over 18 bodies and two passes) and about 290 bone rows a render; fighting adds
about 1.5 ms a pass over standing (combat AI, hit reactions, animation blending - not separated).
The engine's share is spread over the pass: palette building, shader constant uploads (0x007C9140
transforms three light slots at 0x00B45040 into constants at 0x00B44FD8 for every draw), culling
(0x0047DA70 plane against sphere from 0x007D2EA0), the stream packer.

The rest of the measured cost was OBVR's own bone lock (the dual pass pairs the second render's
rows with the first render's), 2.2 to 3.8 samples a frame with eighteen bodies until 2026-10-26,
when its log was sized for crowds and indexed; it is OBVR's, not the engine's (performance-spec
section 7).

## Unverified

- 0x00802AE0: a bound over a vertex array (16-byte vertices, a count at +0xe, min/max then centre
  and radius through vtable slots +0x9c and +0xa0 of an object looked up in the list at
  0x00B47878 or the map at 0x00B4786C); it stood on the stack under OBVR's constant hook with
  eighteen Walkers. Which object, and whether it is per skin or per particle system, is not read.
- Whether the palette builder runs once a frame for both eyes (the frame stamp says once a
  frame) or once a render: OBVR's second render re-uploads the same rows, so the upload is a
  render's, the build a frame's - derived from the stamp, not traced.
- The Windows (native D3D9) cost of the same scene: Nadi's Pimax run of 2026-10-26 had 2753 to
  6645 bone rows a frame with eighteen Walkers in sight and 22 to 23 ms frames at 90 Hz; the
  sampler was not on in that stretch.
