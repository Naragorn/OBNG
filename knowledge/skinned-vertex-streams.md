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
| 40 % | the rest of the engine's render walk: 0x0077A1F0 on the stack in 49 % of samples, 0x00767520 in 40 % |
| 4 % | ntdll and kernel32: locks (0x006FF9C0), file reads |

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
- It calls **the copier** 0x0076F590, which works out the offsets per
  stream. The copier calls **the stream copy** 0x0076E610, which has three
  paths:
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
  0x00767B40: 0x00771790 and 0x00773330 call the packer 0x0076FE20, not
  0x00763130. Measured: another 0.5 to 0.6 MB a pass, in 210 small copies.
- **A staging copy at every unlock.** Every dynamic buffer is written
  through a system-memory staging copy:
  - The lock wrapper 0x00776C90 hands out `[this+0x40]`.
  - The unlock 0x00776D80 copies that staging memory into the mapped D3D
    pointer `[this+0x48]` with `rep movsd` at 0x00776D8F.
  - Every byte packed is therefore written twice.
- **The dynamic-buffer ring manager** is 0x007829A0,
  `thiscall(ring, bytes, ...)`, ret 14h. It creates every dynamic vertex
  buffer the engine makes, 2048 to 120384 bytes, at 0x00782A08.
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
