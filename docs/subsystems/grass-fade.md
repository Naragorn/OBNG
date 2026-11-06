# Grass fade: by the distance, not by the clip-space length

`[Fix] GrassFade` (default 1), `[Fix] GrassFadeReach` (game | ini). Code:
`plugin/src/GrassFadePatchLogic.h` (the decisions and the patch),
`plugin/src/GrassFadeFix.cpp` (the device side). Tests:
`plugin/tests/GrassFadePatchLogicTest.cpp`,
`plugin/tests/GrassFadePatchPackageTest.cpp` (against the installed game's
shader packages).

## The fault

Nadi, Windows headset, 2026-11-05: "the distance at which grass is still
drawn shrinks and grows as I look left or right."

**Read** (shader package 019, the 24 `GRASS20xx.vso`, disassembled with
`vkd3d-compiler -x d3dbc -b d3d-asm`): the blade's fade is taken from the
length of its clip-space position, `dp4 r, clip, clip`, against AlphaParam
c5 (`(len - c5.x) / c5.y` and `(len - c5.z) / c5.w`). In view-space terms
that length is sqrt((x / tanW)^2 + (y / tanH)^2 + z^2 + w^2): 1.41 times the
distance to the camera in the middle of the view, sqrt(3 / (1 + tanW^2))
times at the picture's horizontal edge - 1.22 at the game's 75 degrees,
1.14 at the Linux harness's frustum (tangent 1.152), 1.01 at the Windows
headset's (1.402).

**Measured** (the grass probe, `[Probe] Grass`: the device's c5, c9..c12 and
blade positions c20..c42 read back at every grass draw of the world pass,
the shader's fade computed from them; Nadi's save 15 at Vilverin,
fGrassEndDistance 8000 / start 7000, the head turned all around; runs
`obng-sewer-exit-single/20261106-111726` and
`obng-sewer-exit-dual/20261106-111906`): between 5000 and 6000 units the
mean fade is 0.53 to 0.57 in the middle third of the picture and 0.99 to
1.00 in the third beside it, the single pass and the dual pass alike. A
clump there comes and goes as the head turns. Not a fault of the single
pass.

## The fix

OBNG replaces each grass vertex shader, as the game binds it, by a twin
that takes the length of the view-space position. The twin needs no
constant of its own: the first two rows of ModelViewProj (c9, c10) carry
the projection's two scales as the length of their xyz, so

    mov rA.xyz, c9      dp3 rA.x, rA, rA      ; P11^2
    mov rB.xyz, c10     dp3 rA.y, rB, rB      ; P22^2
    rsq rA.x, rA.x      rsq rA.y, rA.y
    mul rA.xy, rClip, rA                      ; view x, view y
    mov rA.z, rClip.w                         ; view depth
    dp3 rLen, rA, rA                          ; in place of dp4 rLen, rClip, rClip
    add rLen, rLen, rLen                      ; the game's reach only

Twice the squared distance is what the game has in the middle of the view,
so with the add (`GrassFadeReach=game`, the default) grass reaches as far
as the game has it straight ahead - about 0.71 x fGrassEndDistance - and
now as far everywhere else in the picture. Without it (`ini`) the two INI
values are distances in units and grass reaches 41 % further.

A grass vertex shader is known by structure, not by a byte image: version
2.0 or later, a constant table naming AlphaParam, ModelViewProj,
InstanceData and WindData, exactly one dp4 of a temp register with itself
into one component, and that register moved to oPos. The distant trees'
`DISTLOD2002/2003.vso` name the first three constants too and are left
alone (no WindData, no such dp4).

The device side: CreateVertexShader (slot 91) and SetVertexShader (slot 92)
in the device's method table, hooked by a thread that waits for the
renderer's device. A shader bound for the first time is asked for its byte
code; a twin is created through the method table (FoliageAA, hooked there
too, sees it and knows it as a grass shader by its constant table, which
the twin keeps); a bind of the shader binds the twin. A direct-mapped table
without a lock stands in front of the record, since the hook runs for every
bind of a frame.

## What did not work first

The first twin scaled the clip vector by a constant of its own in c245,
uploaded at the bind. **Measured**, run
`obng-single-pass-meadow/20261106-120731`: at the grass draw c245 held
(56802.4, 88956.4, 627.97, 1.42) - an instance's position. The engine
uploads the instances' data from c20 far beyond the two registers the
shader's table gives InstanceData and the 23 its constant map lists, and
twelve of the 24 grass shaders name every float register below c20 (table
and literals together), so there is no register to have. The pictures of
that state showed grass missing near the middle of the view (runs
`obng-single-pass-meadow/20261106-114007` against `-114156`).

## Checked

- **The twin's arithmetic**: `GrassFadePatchLogicTest` runs the patched
  byte code of a hand-written grass shader in a small machine - twice the
  squared distance in the middle, at the edge and in a corner of the
  picture, the squared distance for `ini`, the position and every other
  register as the original leaves them.
- **The game's shaders**: `GrassFadePatchPackageTest` over all 19 shader
  packages of the installed game - every GRASS*.vso of version 2.0 or later
  patched (24 a package; package 001 has twelve 1.1 shaders, left alone),
  no other vertex shader patched, every twin its original with the patch at
  the dp4. All 24 twins of package 019 disassemble, each with the one dp4
  replaced by the ten instructions above.
- **In the game** (Linux, single pass): the twins are made and bound (two
  at the meadow, one at the sewer exit), also with FoliageAA 2 installed,
  whose log still reports its grass supersampling (run
  `obng-sewer-exit-single/20261106-122545`). The probe's fade with the twin
  bound, meadow, 5000 to 6000 units: 0.27 / 0.14 / 0.44 for the middle /
  between / outer third where the game's shader has 0.41 / 0.70 / 0.99, and
  0.00 everywhere from 6000 on where the game's shader has up to 0.36 (runs
  `obng-single-pass-meadow/20261106-121800` and `-121951`). These are the
  probe's computation of what the twin does with the device's constants,
  not pixels.
- **Not shown in pictures**: the eye pictures with and without the fix
  differ by noise only at the sewer exit (under one percent of the pixels,
  spread over the picture) and are not comparable at the meadow (the
  weather differs between runs). In the harness's quarter-size pictures and
  its narrower frustum the grass in the fade zone is a few pixels. The
  headset is the check.

## Limits

- A symmetric frustum and a world matrix without scale are assumed (rows 0
  and 1 of ModelViewProj then have the lengths P11 and P22); the game's and
  OBVR's frusta are symmetric, the grass's world matrix has no scale
  (measured on one shader: |c9.xyz| 0.868 with P11 0.8684).
- The fog of the same shaders is the three-component length of the clip
  position and has the same kind of dependence; not changed.
- The tree leaves' vertex shaders (`STLEAF000..003.vso`, version 1.1) take
  a four-component length too; what it feeds is unread, and they are not
  touched.
