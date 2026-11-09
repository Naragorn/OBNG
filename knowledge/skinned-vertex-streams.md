# Skinned bodies' vertex streams, and where a scene pass goes

Status: knowledge, written 2026-10-25 from OBVR (`../OBVR`,
docs/performance-spec.md sections 0, 1, 4 and 5; `src/render/StreamCopy*`,
`src/render/StreamReuse*`). Nothing of this is built in OBNG. OBVR built
two of the levers named below.

Marks: **read** = read in Oblivion.exe 1.2.0.416 (Ghidra/REA, the function
starts exported in `../OBVR/tools/oblivion-1.2.0.416-functions.tsv.gz`),
**measured** = measured in the game, **derived**, **unverified**.

Machine for every measurement: Linux, Proton, DXVK, RTX 4090, OBVR's dual
pass (the scene rendered once per eye), 75 Hz, headless harness. A monitor
player renders one pass a frame.

## What a person costs (measured)

- The opening's five people (the Emperor, three Blades, Valen Dreth), in
  view under the dual pass, measured on 2026-10-19:
  - A scene pass takes 2.85 ms with them and 0.72 ms with nobody in view.
    That is about **0.43 ms of engine CPU per person and pass**.
  - A person in armour costs about twice a monk. Three Blades draw 20
    skinned draws each.
- Interiors are cheap without people. Weynon Priory's house runs a 3.5 ms
  scene pass with its 95 skinned draws, and its people are behind the
  player.

## Where a pass with five people goes, by engine function (measured)

OBVR's render sampler took the render thread's eip and stack about every
millisecond: 11396 in-pass samples, a scene pass of 3.22 ms with the sampler
on (run of 2026-10-23 14:59).

| Share of a pass | What |
|---:|---|
| 24.0 % | the CRT's `memcpy` (0x009812C0): 79 % of it called per vertex from the indexed stream copy 0x0076E610, 19 % from the buffer unlock 0x00776D80 |
| 7.9 % | the stream copy's own loops, called from the copier 0x0076F590 |
| 2.5 % | 0x0076F3B0, the copier's memset loop |
| 13.1 % | DXVK: draws, constants, the unlock |
| 4.6 % | OBVR.dll |
| 3.8 % | 0x0047DA70, a sphere against a plane (culling, from 0x007D2EA0; the roles corrected 2026-10-30, knowledge/bone-palettes.md) |
| 40 % | the rest of the engine's render walk: 0x0077A1F0 on the stack in 49 % of samples, 0x00767520 in 40 % (corrected 2026-10-30: 0x0077A1F0 is NiD3DShader::SetupShaderPrograms, slot 14, called per partition and pass from the draws - program binding and constant maps, not a walk; 0x00767520 is NiDX9Renderer::DrawSkinnedPrimitive; knowledge/culling-and-draw-setup.md) |
| 4 % | ntdll and kernel32: locks (0x006FF9C0 is NiObjectNET::GetExtraData, a critical section on 0x00B3F600 - read 2026-10-30), file reads |

The allocator and the critical sections together take **under 0.5 %** of a
pass (measured). A heap replacement or lock hooks, the approach of Oblivion
Stutter Remover, find nothing to speed up here (derived).

## The stream packing: what the engine does (read)

**Every pass, the engine copies every skinned body's vertex streams into
its dynamic vertex buffers again.** Whether it also transforms them on the
CPU is unverified (see the end). These figures are for five people (measured):
- 3.7 MB copied per pass.
- 135 dynamic-buffer unlocks per pass, against 6 with nobody in view.

- **The stream packer** is 0x00763130,
  `thiscall(renderer, record, geometry, a3, desc, custom, force)`. It
  returns al=1.
- It reaches **the copier** 0x0076F590 (read 2026-10-30: NiDX9ShaderDeclaration::PackEntry,
  `__stdcall` with seven arguments, called once per declaration entry from
  the declaration's vtable slot +0x6C = 0x0076FE20 - there is no direct call
  from 0x00763130; the earlier note that 0x00771790 / 0x00773330 reach
  0x0076FE20 through that slot was a sampler artefact - 0x00773330 is
  TextureStageGroup::ApplySamplerStates and touches no stream, see
  re/functions/0x00773330.md and OBVR's performance-spec.md:743 - the second
  entry is the whole-geometry arm 0x00767B40 under 0x0077A310). PackEntry builds the descriptor and calls **the stream copy**
  0x0076E610 only with a non-null source, so the zero-fill path below is
  not reached from it. The stream copy has three paths:
  - **zero-fill**, the copier's memset loop 0x0076F3B0;
  - **straight runs**, with unrolled 8, 12 and 16-byte copies;
  - **indexed**, the path skinned bodies take. Its loop at 0x0076E690
    reads the partition's index, computes the source and **calls `memcpy`
    once per vertex for 8 or 12 bytes**. That loop is the single largest
    item in the table above.
- **The stream copy 0x0076E610 at instruction level** (read 2026-10-30,
  `re/functions/0x0076E610.md`, held by three skeptics; proposed
  `CopyVertexStreamElements`): cdecl with one argument, the copier's 0x28-byte
  descriptor (OBVR's `StreamCopyDesc` layout confirmed field for field; the
  type code at +0x00 is never read; `count` and `sourceWords` are words);
  ECX is not an input; six plain `RET`; returns the running total. Path
  order: a null source (+0x10) takes the zero-fill **before** the index test;
  then an index table (+0x0C) takes the indexed gather; otherwise the
  straight run, inline dword moves for exactly 8, 12 and 16 bytes and a
  `memcpy` loop for every other size (also 4). The 8/12/16 paths return
  `count * countedBytes` from one 32-bit IMUL, the other paths sum it per
  element (equal modulo 2^32). Count 0 returns 0 and writes nothing; `dest`
  is never null-tested. The function reads no global and no clock.
  Ghidra had no function here (it is reached through the routine table that
  0x00771300 fills, pseudocode: the same-format copy for formats 1-4);
  created 2026-10-30.
  - **Finding for OBVR** (`StreamCopyLogic.h`, `StreamCopyFast` and
    `StreamCopyReference`): on the **zero-fill path the engine memsets
    `countedBytes` (+0x1C) per element, not `elementBytes`** (read:
    0x0076E642/0x0076E64E load [EDI+0x1C] as memset's size; +0x14 is not read
    on that path). OBVR's reference and fast copy memset `elementBytes`.
    The in-game check (202000 copies, 0 mismatches) cannot see this when
    `countedBytes >= elementBytes` (the engine's extra zeroes survive or lie
    outside the compared span), and the zero-fill path may never run from
    0x0076F590 (its pseudocode calls the routine only with a non-null source,
    unverified). It matters only if a caller passes a null source with
    `countedBytes != elementBytes` - open. Every other path matches OBVR's
    reference (source address, size, dest step, total, path order); OBVR's
    extra inline size-4 case is an optimisation, not a difference.
  - The line above calling 0x0076F3B0 "the copier's memset loop" is doubtful:
    in the decompiled table filler it is another table routine with the
    same descriptor shape whose null-source path memsets **0xFF**, not 0
    (unverified, pseudocode of 0x0076F3B0 and 0x00771300). OBVR's
    performance-spec carries the same wording.
- **A second entry into the packing** comes from the shape render
  0x00767B40 (the whole-geometry arm under 0x0077A310, read by OBVR
  2026-10-30; the earlier attribution to 0x00771790 / 0x00773330 was a stale
  return word, re/functions/0x00773330.md C16-C18). Measured: another 0.5 to
  0.6 MB a pass, in 210 small copies.
- **A staging copy at every unlock.** Every dynamic buffer is written
  through a system-memory staging copy:
  - The lock wrapper 0x00776C90 hands out `[this+0x40]`.
  - The unlock 0x00776D80 copies that staging memory into the mapped D3D
    pointer `[this+0x48]` with `rep movsd` at 0x00776D8F.
  - Every byte packed is therefore written twice.
- **The dynamic-buffer ring** is 0x007829A0 (DynamicVertexBufferRing::Reserve,
  proposal; re/functions/0x007829A0.md, read 2026-11-04),
  `thiscall(ring; bytes, ...)`, ret 14h, called only from
  NiDynamicGeometryGroup's allocate slot 0x0077E430. It hands out a slice of
  the ring's one vertex buffer from the cursor +0x14 and returns the lock
  flags for it: NOOVERWRITE (0x1000) while appending, DISCARD (0x2000)
  forced at offset 0 (the wrap). It does not lock. It creates every dynamic
  vertex buffer the engine makes, 2048 to 120384 bytes, at 0x00782A08.
  - The ring's capacity at +0x08 is the **largest single request seen**,
    rounded to 16, plus 32.
  - A bigger request frees the buffer and creates a new one of that size,
    with usage 0x208 or 0x218.
  - A request that fits behind the used mark at +0x14 locks with
    NOOVERWRITE (0x1000). One that does not fit resets the mark and locks
    with DISCARD (0x2000).

### What the staging copy does under DXVK (measured, partly unverified)

- In one run the unlock's `rep movsd` took 1.2 ms a pass; in others it
  took 0.15 ms.
- OBVR's spec explains this with DXVK: it places a WRITEONLY dynamic
  buffer in device-local, host-visible memory when it can (DXVK's
  `D3D9CommonBuffer::CreateBuffer`, from deepwiki on doitsujin/dxvk). A
  dword copy into VRAM over the BAR then runs at PCIe speed.
- Which placement a run gets varied from start to start. This was read from
  the copy rates, not from a DXVK log (unverified beyond that).
- `d3d9.cachedWriteOnlyBuffers = True` in dxvk.conf keeps those buffers in
  cached system memory and removes the 1.2 ms case (measured).

## What OBVR changed, and what it gained (measured)

- **The fast stream copy** (OBVR `[Render] FastStreamCopy=1`, also in the
  plugin "Oblivion Frame Fixes"). An entry detour on 0x0076E610 with the
  same contract; the element size is decided once per call, not per vertex.
  - Checked against the engine's routine in the game: 202000 copies,
    0 mismatches.
  - The memcpy-per-vertex plus the loop went from 27 % of a pass to 10 %.
  - Together with the dxvk.conf line, the scene pass went from 3.22 ms to
    2.13–2.57 ms.
  - Flat, one pass a frame like a monitor player: 5.43 ms down to 5.06 ms
    a frame (7 % of the work).
- **Skipping the second eye's packing** (lever 2, `[Render]
  StreamReuse`, off by default). The second pass reuses the first pass's
  packed streams when no DISCARD has hit the buffers since:
  - 98539 packs compared, 0 mismatches.
  - 92 % of the second pass's packs skipped; its copies fell from 1.37 MB
    a frame to nothing.
  - **No measurable gain.** After the fast copy, those copies were worth
    only about 0.14 ms a frame, and the second entry 0x0076FE20 stays
    unseen.
  - Lesson (derived): after the fast copy, the remaining cost is the
    engine's render walk, not the bytes.

## What a modern engine does instead

- **Skin on the GPU.** The bind-pose vertices stay in static GPU buffers;
  only the bone matrices change per frame. They are uploaded to a constant
  or structured buffer, and the vertex shader or a compute pass does the
  skinning. Nothing of the mesh is repacked on the CPU.
  - Unreal Engine's `FGPUSkinCache` dispatches compute work per skinned
    section into GPU read-write position and tangent buffers (`Dispatch`,
    `GetPositionBuffer`, `GetPreviousPositionBuffer`, `GetTangentBuffer`).
    Read in the API listing
    https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FGPUSkinCache;
    the general skeletal paths are on
    https://dev.epicgames.com/documentation/unreal-engine/skeletal-mesh-rendering-paths-in-unreal-engine
    (not read beyond the title).
- **Write dynamic data straight into persistently mapped upload memory**,
  once, not through a staging copy. Size the rings to a frame's volume, not
  to the largest single request (derived from the ring manager above; not
  sourced from an engine).
- For OBNG (derived): Oblivion already sends bone palettes to the vertex
  shader as constants, and OBVR's bone lock hooks exactly those
  `SetVertexShaderConstantF` rows. Whether the engine's skinned shaders
  skin on the GPU and the CPU copy only re-packs streams, or whether the
  CPU also transforms the vertices, is **unverified**. Settle that first by
  reading the skinned draw path from 0x00767B40.

## A crowd's pass is the volatile packing (2026-11-09 21:50, runs obng-actors-count/20261109-183942 and -215051)

Measured on the actor-count scenario (Gold Road spot 2, then 20 / 40 / 60 of
the opening's cast placed in view; Linux, DXVK, single pass, Oblivion Frame
Fixes' fast copy on):

- **OBVR's render sampler cut by the marks** (tools/re/sampler-phases.py,
  run -183942): the in-pass samples go from 3.6 to 15.2 a frame (about ms)
  between the empty road and 60 placed NPCs. Of the 11.6 gained: Oblivion
  Frame Fixes' stream copy +3.7, the CRT memcpy 0x009812C0 +1.5 (the
  unlock's staging copy and the copier), 0x0076F3B0 +0.45, D3D9 (DXVK)
  +1.3, OBNG.dll +0.4, OBVR.dll +0.3; everything else of the engine's draw
  walk together under 2. Half of a crowd's pass is copying vertex bytes.
- **The pack-cache probe** ([Probe] PackCache, run -215051; the probe's
  hashing slows the run, the counts stand): no volatile pack at all on the
  road with its 64 actors (the Walkers' bodies and the creatures are
  static data: 1 180 early-outs a frame); with the cast placed, **one
  volatile pack per placed NPC and frame** - 61 packs a frame at 60 NPCs,
  each a 0.3 MB staging block, 18 MB a frame, every one reserved at the
  ring's offset 0 and locked with DISCARD (the slice the same every time).
  40-90 % of the packs carry the bytes of that key's previous pack (90 %
  right after a spawn, 40-45 % with all 60 moving and talking).
- So the opening's cast carries one volatile geometry each that the road's
  actors do not (the FaceGen head is the candidate: unread), and each costs
  a 0.3 MB pack plus the staging copy every frame, about 90 us an NPC
  (derived: 5.6 ms of copy samples over 61 packs).

What follows for the lever (docs/subsystems/stream-packing.md, L3): its
ceiling was 0.4 ms on the Walkers scene and is 2-5 ms a frame in a crowd of
named NPCs. Two separate gains: (a) skipping the pack whose source did not
change - needs the change signal (who writes a volatile geometry's vertex
arrays: the head's morph, unread) and a buffer that survives between
frames (the ring's DISCARD renames the bytes every pack: the route must
own those chips); (b) the size of a pack - 0.3 MB for one head is more
than its vertices should need (unread: whether the whole chip is copied at
the unlock whatever was written).

### What a volatile pack is, and a change signal that does not hold (2026-11-09 21:57-22:05, runs -215701, -220046)

- **The pack's size is the mesh's** (run -215701, the probe's description
  of the first volatile packs): every one is a geometry data of 3 515
  vertices with a skin of 5 bones, stream 0, 309 320 bytes locked - 88 bytes
  a vertex, the whole chip; packed under two declarations (0x12004188 and
  0x120007E0 in that run). The unlock copies exactly the locked bytes (read,
  re/functions/0x00776D80.md). So nothing is over-copied: the block is one
  mesh per named NPC, the same vertex count for every one of the cast (the
  head is the candidate; the name is unread).
- **BSFaceGenNiNode's gated morph 0x0055D1B0 is not the change signal**
  (run -220046: the probe stamped every child geometry's data when
  0x0055D1B0 ran and compared with the packs' verdicts): with 60 NPCs,
  3 500-4 600 morph calls a window against 12 000-14 000 volatile packs, and
  of the packs whose data was *not* stamped since their last pack, 5 400-
  6 500 were the same and **700-6 900 changed**. Either the volatile data is
  not a direct child's of the head node, or another writer changes it (the
  gate of 0x0055D1B0 lets only near or important actors through; the
  animation step vfunc_DC / vfunc_8C runs for all), or the changes are not
  the source's (the two declarations sharing one staging block: a pack under
  one layout leaves the other's bytes behind). Unread; the signal needs the
  writers of that data's vertex array found by reading, not by guess.
- **The modify bracket 0x00728AB0 is not the signal either** (run -220731:
  the probe stamped the bracket's object and the one at its +0x34 on every
  opening, 18 000-22 000 a window): unstamped packs 5 000-6 700 same and
  1 100-6 600 changed, the same picture as with the morph. In both runs
  the unchanged packs stay near 6 000 a window (24 a frame) whatever the
  NPC count, and the changed ones grow with it (20 -> 60 NPCs: 1 100 ->
  6 500). Neither guess names the writer. What settles it without another
  guess: hash the source arrays (the data's vertex and normal pointers) at
  the packer's entry beside the packed bytes - a pack that changes with an
  unchanged source is the packer's own doing (a skinned blend on the CPU
  for this 5-bone mesh, or stale staging bytes), one that changes with the
  source has a writer still to find.

### The changed packs are 16 bytes of the vertex the source does not explain (2026-11-09 22:11-22:20, runs -221154, -221500)

- **Source against packed bytes** (run -221154: the data's vertex and normal
  arrays hashed at the packer's entry, compared with the unlock's verdict,
  per 10 s window at 20 / 40 / 60 NPCs): with the source as at the key's
  last pack, 4 900-6 900 packs the same and **700-7 300 changed**; with a
  changed source 80-260 packs, every one changed. So the head's vertices
  really change in about one pack a frame (the morph), and thousands of
  packs change with an unchanged source.
- **Which bytes** (run -221500: the last staging block kept per key, the
  changed packs compared byte by byte, per 88-byte vertex): 27 % of the
  vertices differ, and nearly all of it at **offsets 40..55** (25-32
  million byte differences a window in each of the four dwords there)
  against 0.1-0.2 million at offsets 0..11 (the position: the real morphs);
  nothing anywhere else - not the normal, not the rest.
- Derived, to be read: a 16-byte element at +40 that one of the two
  declarations does not write (or writes from another array), so the
  staging block's leftovers from the previous pack - another NPC's head,
  the same topology with another face - stay in it; OBVR's spec noted
  "staging garbage in the gaps" for this path. If the declaration that
  leaves them does not read them either, 98 % of a crowd's volatile packs
  write nothing new that is used.
- **What this makes of lever L3**: a pack is redundant when the source
  arrays did not change; that is knowable for the price of hashing 84 KB
  (about a tenth of the pack) until a writer-side signal is read. Open
  before a build: the two declarations' entries (which element is +40, who
  reads it), and that the chip's buffer keeps its bytes while its pack is
  skipped (the probe saw one buffer per key and the slice never moving).

### The declarations, the gap named, and why a skip alone cannot work (2026-11-09 22:21-22:40, runs -222143, -222808, -223128, -223414)

- **Both declarations of the head have one layout** (run -222143, stride
  88): position +0 (selector 0), blend weights +12 (1), blend indices +24
  (2), normal +28 (3), **colour +40 (4, 16 bytes)**, texture coordinates +56
  (5), two extra-data entries +64 and +76 (negative selectors). The data has
  no colour array (+0x24 null) and PackEntry writes an entry only from a
  non-null source, so the colour entry is never written: the 16 bytes that
  differ between packs are the staging block's leftovers. Gate on that:
  29 377 changed packs with unchanged positions and normals, **0 with a
  difference outside bytes 40..55**. A head's pack is redundant whenever
  its positions and normals did not change - 98 % of a crowd's.
- **But a volatile pack's bytes do not survive the next one** (the route's
  gate, [Shadow] VolatilePack, runs -222808 and -223414: every unlock of a
  dynamic buffer names its writer): of 21 000-23 000 volatile packer calls
  a window, 20 993-22 920 found their record's vertex buffer written by
  another record since their own last pack. All heads of one stride share
  the dynamic ring's one vertex buffer, each pack takes offset 0 with
  DISCARD and is drawn before the next one overwrites it. (The earlier
  "one buffer per key" of the pack-cache probe was the keys' count, not
  proof of separate buffers.) Presenting the data to the packer as mutable
  instead of volatile does not change it: the record's chips still come
  from the dynamic group (run -223414, the same counts).
- So the skip needs buffers of its own: either the head's record moved to
  a static group (where that is decided is unread), or the route gives each
  such record a private vertex buffer and lets the engine's own mutable
  pack fill it (the chip's buffer pointer swapped; the device's Reset must
  release them - the occlusion culler's reset handling is the pattern).
  Medium, with the risk in the lifetime of those buffers; the gain is the
  5-6 ms of a 60-head crowd (1-2 ms with 10-20 named NPCs in view) on
  Linux, unmeasured on Windows.
- The gate's comparison itself held where it applied: 471 and 952
  would-skip packs verified, 0 mismatches. [Route] VolatilePack stays off:
  as built it skips next to nothing.
- The actor-count scenario still fails to draw the crowd in about one run
  of four (run -223128: 8 000 skinned draws a window against 160 000).
