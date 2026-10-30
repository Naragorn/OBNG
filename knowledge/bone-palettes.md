# Bone palettes: how the renderer uploads skinned bodies, and what a crowd costs

Status: knowledge, written 2026-10-26 from OBVR (`../OBVR`: `src/render/BoneRebase.h`,
`src/render/InterfaceRenderHook.cpp` HookedSetVsConstantF, docs/performance-spec.md section 7;
the Walkers session's capture `../OblivionWalkers/artifacts/hand-script/perf-walkers-in-view/20261026-141551`
and OBVR's `artifacts/hand-script/perf-walkers-fight/20261026-145000` and `-145514`). Ghidra 12.1.4
decompilation through REA. Corrected 2026-10-30 from the perf-levers slice (instruction-level
mappings held by three skeptics each: `re/functions/0x007655F0.md`, `0x007C9140.md`, `0x0047DA70.md`).
Nothing of it is built in OBNG.

Marks: **read** = read in Oblivion.exe 1.2.0.416 (Ghidra decompiler), **measured** = measured in the game
(OBVR's hand-script harness, the render sampler), **derived** = follows from the two,
**unverified** = not checked.

## The palette builder 0x007655F0 (read; proposed `Renderer::BuildBonePalette`)

A thiscall on the renderer (`ecx` carries the renderer; `[ecx+0x8ac]` is an object whose vtable slot
+0xfc is called at the end with a flag) with **five** stack arguments, `RET 0x14` (read 2026-10-30;
the earlier "one argument" was the decompiler's): `(skin, world, rowTranslation, mode,
separateSkinToWorld)` - arg2 a 13-float transform composed into the bones, arg3 a byte that picks
the mode-4 layout and packer, arg4 the mode compared with 3 and 4 and stored at +0x24, arg5 a byte
that routes the skin-to-world matrix into the record's scratch matrix instead of every bone. The
callers (pseudocode, unverified): 0x00779710 passes `(skin, _, 0, 4, 0)`, 0x007C9230 `(skin, _, 0, 3, 1)`.
`param_1` is a skin instance record:

| Field | Meaning |
|---|---|
| +0x08 | the skin data; its +0x40 is the bone count, its +0x44 the per-bone bind transforms, 0x4c bytes each (a 3x3 rotation, a translation, a scale: thirteen floats, then the bound) |
| +0x14 | the bone node array (per bone a pointer; the node's +0x94 is its world scale) |
| +0x18 | the build stamp: the value 0x00422DE0 returns on the object at renderer+0x8B0 (read: compared for **equality** at 0x00765614, any other value rebuilds; written back from a second call at 0x00765638). That the value is a frame counter is **unverified** - OBVR's GameAddresses.h:1463 calls 0x00422DE0 the `+0x3C` niNode getter, and its body is `return [ecx+0x3C]`; what object sits at renderer+0x8B0 decides whether the stamp is a counter or an identity key. **OBVR should pick this up** (performance-spec section 7 says "frame stamp") |
| +0x1c / +0x20 / +0x24 | the palette buffer's capacity, pointer and mode; reallocated through 0x00401F00 / 0x00401F20 when the bone count grows or the mode changes |
| +0x28 | a 0x40-byte scratch transform, allocated on first use, filled by 0x00761BE0 |

Per bone the builder composes the bone's world transform with the bind transform (0x0053D7A0,
`T = node.world ∘ bind[i]`, 0x00765810; when arg5 is 0 once more with the skin's transform `U`,
0x0076585D) and writes a 4x3 row of twelve floats (read 2026-10-30, 0x007658D1-0x0076594B): the
rotation **as stored, not transposed, every entry multiplied by the composed transform's own scale
T[12]**, and floats 3, 7, 11 the translation **not scaled**, minus the global vector at
0x00B3F92C/30/34 (0.0 in the file image; its runtime writer is unverified - if it is a world-origin
rebase it matters to OBVR's BoneRebase.h). The earlier "scaled by the skin's world scale" was wrong:
no world scale of the skin is read; s is the per-bone composed scale (node scale times bind scale,
times U's scale when arg5 is 0). In mode 4 the same four values go to the packers
0x00761B60 (arg3 set) / 0x00761AE0 (arg3 clear) as `(dest, &R, &t, s)`, cdecl; mode 3 takes the
4x3 path like every mode other than 4. The buffer is reallocated only when the bone count grows past
the capacity or the mode changes (unsigned compare, 0x0076565A-0x00765660), never shrunk. The rows go to the GPU
as vertex shader constants, three float4 registers a bone, in three register classes (OBVR's
census, read from the uploads): skin shaders at c42 upward (rows on registers divisible by 3),
hair at c31 (1 mod 3), head parts - eyes, teeth, some hair - at c14 (2 mod 3). The flag handed to the
renderer slot +0xfc is **always 0 as a byte** (read 2026-10-30: the two writes of 1 at 0x0076578A and
0x0076584E need the tested scale to be below 0.99 and above 1.01 at once, NaN takes the parity
jump; the pushed dword's upper three bytes are stale stack contents). The earlier reading of a live
"non-uniform scale flag" was wrong; **OBVR should pick this up** (performance-spec section 7). What
the slot does with a flag that is always 0 is unverified.

Cost shape (derived from the loop): per bone one 0x0053D7A0 call when arg5 is set, two plus a
13-dword copy when it is not, and one x87 compare that can never change the flag; per build one
inverse (0x00718A80), two composes, two copies and, with arg5, one 0x00761BE0. The callees worth
their own slice for a bit-exact rebuild: 0x0053D7A0 (the hot compose, its x87 order decides every
palette float), the three packers 0x00761AE0/0x00761B60/0x00761BE0 and the inverse 0x00718A80.

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
The engine's share is spread over the pass: palette building, shader constant uploads (0x007C9140,
below), culling (0x0047DA70, below; the sphere against a plane), the stream packer
(knowledge/skinned-vertex-streams.md).

## The light constants per draw 0x007C9140 (read 2026-10-30; proposed `UpdateLightPositionConstants`; rebuilt and verified in the emulator, 27 cases)

`__stdcall(const D3DXMATRIX* matrix, float scale, int passId, int skipAdjust)`, `RET 0x10`, no
`this`, one callee (D3DXVec3TransformCoord, D3DX9_27.DLL), three callers (0x007F6FC0, 0x007C9230,
0x007F7EE0). For each of three slots: when the record at `[0x00B45040 + 4i]` has a non-zero byte at
+0x08, the three floats at `0x00B46528 + 0x10i` are transformed as a point by the matrix, a fourth
float comes from `0x00B465A8 + 0x10i`, and - only when arg4 is 0 - either the point is multiplied
by arg2 (arg3 in 0x177..0x179) or the fourth float is divided by it (any other arg3); the four
floats are stored as one vec4 at `0x00B44FD8 + 0x10i`. A slot whose byte is 0 keeps its previous
vec4. Correction to the earlier wording: the records at 0x00B45040 are only **gated** on; the
positions come from 0x00B46528/0x00B465A8. No clock, no device call, no allocation.

Lever (derived): the output is a function of (slot flags, the two tables, the 64 bytes of the
matrix, arg2, whether arg3 is in range, arg4 != 0) plus the previous vec4 of a disabled slot - so
a per-draw cache keyed on those values is exact, under three preconditions: the x87 control word
is the same between calls (the body has no FLDCW), D3DXVec3TransformCoord is pure (unverified,
D3DX not read), and the matrix does not alias the constant block. If the matrix is the drawn
object's inverse world matrix (0x007C9230 pseudocode, unverified), the xyz part changes with every
object and only the w part (lightW, unchanged while arg4 != 0 or arg3 in range) is per-frame. The
per-draw constant setup 0x007C9230 (3320 bytes) decides the arguments and is the slice to map next;
the writers of the two tables and the uploader of 0x00B44FD8 are unknown.

## Sphere against plane 0x0047DA70 (read 2026-10-30; proposed `NiBound::WhichSide`, name unverified; rebuilt and verified in the emulator, 24 cases, plus 94 million inputs against the original bytes on a host x87 with 0 mismatches)

`__thiscall(this = sphere {centre +0x00..+0x08, radius +0x0C}; const plane* {normal +0x00..+0x08,
constant +0x0C})`, `RET 4`, no callees, no globals. Correction: the earlier "plane against sphere"
had the roles reversed - the instructions subtract `[arg]+0x0C` (0x0047DA90) and compare against
`[ECX]+0x0C` (0x0047DA9B, 0x0047DAB3), and the call sites confirm it. It computes
`d = ((n.y*c.y) + (n.x*c.x)) + n.z*c.z` in exactly that order (the decompiler's `(z + x) + y` is
wrong and can differ arbitrarily under cancellation), rounds the sum to single through [ESP+4],
subtracts the plane constant, rounds again, and returns 2 when `d <= -r` (wholly on the negative
side), 1 when `d >= r`, 0 otherwise (straddling, or NaN). A bit-exact rebuild computes in single
precision in that order; at the game's 24-bit precision control the two stores change nothing
in range, and only overflow/underflow at the stores differ from the x87 registers. Source for
that (read 2026-10-30, Intel SDM vol. 1 section 8.1.5.2 "Precision Control Field", mirror
https://xem.github.io/minix86/manual/intel-x86-and-64-manual-vol1/o_7281d5ea06a5b67a-197.html
and archive.org 325462-sdm-vol-1): "The double precision and single precision settings reduce
the size of the significand to 53 bits and 24 bits, respectively. [...] When reduced precision
is specified, the rounding of the significand value clears the unused bits on the right to
zeros. The precision-control bits only affect the results of the following floating-point
instructions: FADD, FADDP, FIADD, FSUB, FSUBP, FISUB, FSUBR, FSUBRP, FISUBR, FMUL, FMULP,
FIMUL, FDIV, FDIVP, FIDIV, FDIVR, FDIVRP, FIDIVR, and FSQRT." So the significand is single
after every FMUL/FADDP/FSUB here, the exponent keeps the register's range (the SDM reduces
only the significand), and FLD/FSTP/FCOMP are untouched by the setting: a product beyond
FLT_MAX stays finite in the register and becomes infinite at the store; one below FLT_MIN is
rounded again to a denormal at the store (derived from the quote).

Lever: the 3.8 % sit in the callers' six-plane loops, not here. 0x0070DFB0 (frustum planes at
+0x2C under the mask +0x8C; 0x0070DFD3 is a branch target inside it, not a second caller) leaves
its loop on result 2; 0x007D2EA0 (the shadow light's range test, planes at this+0x150, mask
+0x1B0) clears its return byte on 2 and **keeps looping**, still clearing mask bits on later
1-results. A batched rebuild of either must keep that difference. 0x0049CBF0 tests a single plane.

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
