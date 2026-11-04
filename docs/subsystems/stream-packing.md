# Skinned-geometry stream packing (design of the replacement of 0x00763130)

OBNG's second subsystem candidate: the per-pass packing of a skin partition's
vertex streams into its dynamic vertex buffers. **Design only; nothing is
built.** Marks: read / measured / derived / unverified (AGENTS.md). Function
files: `re/functions/0x0077A310.md` (PrepareGeometryForRendering),
`0x00763130.md` (PackSkinnedGeometryBuffer), `0x0076FE20.md` (PackStream),
`0x0076F590.md` (PackEntry), `0x0076E610.md` (CopyVertexStreamElements),
`0x00767520.md` (DrawSkinnedPrimitive), `0x007693E0.md` (Do_RenderShape).
All names are proposals (ledger rows 32-49).

## Verdict first

The 1:1 rebuild of the pack path is feasible and verifiable (integer code,
no x87 in its five bodies, a natural bit-for-bit compare point at the
unlock), but as a performance lever it is the weakest subsystem we could
pick: what is left after OBVR's and Oblivion Frame Fixes' fast copy is
small, and the one lever with a ceiling above a few percent of a pass
(keeping packed streams across frames) rests on a fact nobody has measured
yet. Recommendation in section "Recommendation": settle that fact with a
probe (cheap) before building anything; build the 1:1 rebuild only if the
probe opens the cross-frame lever, or as a second method proof if that is
what is wanted.

## The unit

**Entry: PackSkinnedGeometryBuffer 0x00763130** (`__thiscall(renderer;
record, geometryData, skin, partition, decl, force)`, `RET 0x18`, AL;
read, 0x00763130.md C1/C2). One call packs one skin partition: format key,
counts, stream slots, one PackStream 0x0076FE20 per stream through
`decl->vt[+0x6C]` (read, C13/C14), the index refill 0x007781F0 when dirty
has 0x30 (read, C20).

Why not PrepareGeometryForRendering 0x0077A310 (slot 15 of 32 shader
vtables, read, 0x00763130.md C23) as the entry:
- it adds the bind (`SetStreamSource` per stream, `SetIndices`; read,
  0x0077A310.md C9/C10), a device port with no lever behind it (1.8-2.0 %
  of the pass, section "Cost picture");
- its no-partition arm calls the whole-geometry pack 0x00767B40, which is
  mapped from pseudocode only (0x0077A310.md C14, unverified) and would
  have to be rebuilt or forwarded;
- 0x00763130 has the second caller 0x00769030 (`force = 1`, read,
  0x00763130.md C24); detouring 0x00763130 covers both callers with one
  contract. 0x0077A310's gate (C3) stays the engine's.

The rebuild below 0x00763130 is PackStream 0x0076FE20 (the only body of
slot +0x6C, read, 0x00763130.md C14) and PackEntry 0x0076F590 with the
routine table's leaves 0x0076E610, 0x0076F3B0, 0x0076EEB0 and whatever else
the table holds for the (destType, column) pairs that occur (unverified
which occur; open item 4).

## The boundary

In (all read unless marked; file of the claim in brackets):
- the renderer: `+0x8B0` (fallback packer ECX), `+0x8B4` (index buffer
  manager) [0x00763130.md struct table];
- the record (`[partition+0x28]` from 0x0077A310, read 0x0077A354):
  `+0x04` owner (vt `+0x18` allocate, `+0x1C` release), `+0x10`, `+0x1C`
  streamCount, `+0x20` strides, `+0x24` chips, `+0x28`/`+0x2C`/`+0x30`
  index count, bytes, buffer [0x00763130.md, 0x0076FE20.md];
- geometry data: `+0x08` u16 vertex count, `+0x1C` positions, `+0x20`
  normals and the two arrays behind them, `+0x24` colours, `+0x28`/`+0x2C`
  texture sets (pseudocode, unverified), `+0x34` extra data, `+0x2E` u16
  consistency (top nibble) and dirty (low 12 bits) [0x0076F590.md inputs];
- the partition: `+0x08` weights, `+0x0C` u16 vertex map, `+0x10` bone
  indices, `+0x14` triangle list, `+0x18` strip lengths, `+0x1C` vertex
  count, `+0x1E` triangles, `+0x22` strips, `+0x24` bones per vertex;
- the declaration (vtable 0x00A89C74): `+0x0C` buffer manager, `+0x1C`
  entries per stream, `+0x20` stream count, `+0x24` 0x10-byte stream
  records (`+0x08` entries of 0x1C bytes, `+0x0C` stride), slots `+0x60`,
  `+0x68`, `+0x70` [0x0076FE20.md];
- `force` (low byte) and the global tables `[0x00B42700]+4` (routine rows)
  and 0x00B42708 (size per destType; zero in the image, filled at run time
  by 0x0076F9B0, pseudocode only, 0x0076F590.md claim 10);
- the chip per stream: `+0x08` IDirect3DVertexBuffer9, `+0x0C` lock
  offset, `+0x10` lock flags, `+0x14` lock size (role derived);
- the D3D buffers through the lock wrapper 0x00776C90 (D3D Lock, critical
  section `[mgr+0x80]`, staging block `[mgr+0x40]`) and the unlock 0x00776D80
  (memcpy staging to mapped, D3D Unlock) (read, objdump, 0x0076FE20.md).

Out:
- per stream the bytes in the locked range (written to staging, copied to
  the mapped pointer at the unlock; read, 0x0076FE20.md C6);
- the record: `+0x00` format key, counts `+0x14/+0x18/+0x3C/+0x40/+0x44/
  +0x48/+0x4C`, `+0x0C` D3D declaration (refcounted), `+0x20[i]` stride,
  `+0x10`, chips replaced through the owner, index buffer `+0x28..+0x30`
  [0x00763130.md C11/C21, 0x0076FE20.md C11];
- AL (1 usable, 0 failed);
- **not** the dirty bits: 0x00763130 leaves `data+0x2E` alone; the skinned
  draw clears the low 12 bits per pass (`AND word [a2+0x2E],0xF000` at
  0x007677B7, read, 0x00767520.md) and 0x00769030 clears them for static
  data (pseudocode, unverified). The replacement must not clear them either.

Two read facts shape everything below:
- **A volatile geometry gets a new chip on every lock-path pack.**
  PackStream sends `(data+0x2E & 0xF000) == 0x8000` to the reallocation
  0x00776C40 (owner release then allocate) and forces dirty to 0xFFF
  (read, 0x0076FE20.md B10/C8). Whether the owner hands back a new ring
  slice or the same chip is unverified (owner slots unread).
- **Nothing remembers an earlier pack.** 0x0077A310 and 0x00763130 read no
  global (read, 0x0077A310.md C11, 0x00763130.md C28); a volatile partition
  is packed in full on every call that reaches 0x00763130 (derived,
  0x0077A310.md C7/C8; the copies themselves unverified, 0x00763130.md C25).

## Cost picture

What OBVR measured (Linux, Proton, DXVK, RTX 4090, dual pass, 75 Hz;
`../OBVR/docs/performance-spec.md`, cited by line):
- five people: 3.7 MB copied and 135 dynamic-buffer unlocks a pass (6 with
  nobody) (`knowledge/skinned-vertex-streams.md:54-55`; spec line 22);
- run 2026-10-23 14:59, scene pass 3.22 ms: memcpy 24.0 % (79 % per vertex
  from 0x0076E610, 19 % from the unlock), the copy's loops 7.9 %,
  0x0076F3B0 2.5 % (spec lines 554-560);
- the fast copy (OBVR `FastStreamCopy`, the same code in Oblivion Frame
  Fixes): 202 000 copies compared, 0 mismatches; the per-vertex memcpy plus
  loop from 27 % to 10 % of a pass (spec lines 598-600); scene pass 2.57 ms
  with `d3d9.cachedWriteOnlyBuffers = True` in dxvk.conf (run 15:32) and
  2.13 ms (run 15:19); the unlock function 0.15 to 0.2 ms a pass in every
  run but the one whose buffers sat in BAR memory (1.2 ms) (spec lines
  612-622);
- flat, one render a frame: busy 5.43 against 5.06 ms a frame (spec line
  637);
- StreamReuse (the second pass takes the first pass's streams): 98 539
  packs compared, 0 mismatches; 92 % of the second pass's packs skipped,
  its copies from 1.37 MB a frame to nothing; busy 7.31 ms against 7.23 to
  7.64 without: **no measurable gain**; after the fast copy the second
  pass's packing was about 0.14 ms a frame (spec lines 730-746).

What OBNG's own Walkers runs show (**measured**, read 2026-10-30 for this
note from OBVR's render sampler in `artifacts/shadow/obng-range-walk-original/
20261030-122650`, `-143230`, `-144023`, phase people-placed to
people-watched, in-pass samples only; 19 279 / 4 435 / 6 232 samples,
scene_pass means 3.17 / 0.71 / 1.02 ms from `tools/measure-summary.py`).
Oblivion Frame Fixes holds 0x0076E610 in these runs (OBVR.log: "at 0076E610
another plugin's jump ... inside OblivionFrameFixes.dll"), so the copy is
already the fast one. A sample is counted under PrepareGeometryForRendering
when a return address inside 0x0077A310..0x0077A3E2 is on its stack; the
innermost such address before 0x0077A37A means "pack", after it "bind";
the part is then the eip's module or function:

| Share of in-pass samples (run 122650 / 143230 / 144023) | What |
|---|---|
| 14.5 / 11.8 / 14.7 % | 0x0077A310 on the stack (pack + bind) |
| 11.6 / 8.0 / 11.1 % | of it, PackStream / PackEntry / unlock on the stack |
| 5.5 / 3.7 / 5.7 % | the fast copy in OblivionFrameFixes.dll |
| 4.0 / 2.8 / 3.4 % | memcpy called from the unlock 0x00776D80 (the staging copy) |
| 1.8 / 2.0 / 1.8 % | the bind (SetStreamSource / SetIndices into D3D9.DLL) |
| 1.5 / ~1.3 / 1.3 % | 0x0076F3B0 under PackEntry (290 and 83 samples; 143230 not split) |
| 0.4 / 0.25 / 0.45 % | eip in 0x0077A310, 0x00763130, 0x0076FE20, 0x0076F590 themselves (the dispatch) |
| about 0.3-0.5 % | D3D Lock/Unlock, index pack and chip reallocation in D3D9.DLL / OBVR.dll |

Derived (share times scene_pass mean, assuming the sampler is uniform over
the pass): in the heaviest run the whole pack+bind is about 0.46 ms a pass,
the PackStream chain 0.37 ms, the staging memcpy 0.13 ms, the dispatch
0.013 ms; in run 144023 the pack+bind is about 0.15 ms a pass. The frame
is vsync-held (frame means 13.39-13.81 ms against a 13.33 ms period), so a
saving shows in scene_pass and busy time, not in the frame mean. The
sampler scans the stack for code addresses, up to eight
(`../OBVR/src/render/RenderSampler.h:9-14`): stale addresses can add and
the cap can drop frames; per-sample correctness is unverified.

Two corrections fall out of the same samples (derived):
- **0x0077A1F0's "49 % on the stack" includes the packing.** OBVR's
  `tools/perf-functions.py` takes a function to run to the next start in
  `tools/oblivion-1.2.0.416-functions.tsv.gz`; that table has no start
  between 0x0077A1F0 and 0x0077A4A0 (read), so 0x0077A310 is counted as
  0x0077A1F0. In run 122650 the tool's 27.3 % for 0x0077A1F0 splits into
  13.0 % SetupShaderPrograms and 14.3 % PrepareGeometryForRendering.
  `knowledge/culling-and-draw-setup.md` item 5 and spec section 4A read the
  49 % as program binding and constant maps alone.
- **The "second entry" through 0x00771790 / 0x00773330 is not seen here.**
  Of the samples with PackStream, PackEntry or the unlock on the stack,
  2 243 of 2 256, 356 of 358 and 693 of 694 have 0x0077A310 on the stack
  too. Packing outside 0x00763130 does exist, but through 0x0077A310's
  whole-geometry arm 0x00767B40 (0.2-0.5 %), consistent with the spec's
  "out of the shape render 0x00767B40" (spec lines 742-744) and not with
  0x00771790 / 0x00773330 calling 0x0076FE20 (that claim was already
  unverified, 0x00763130.md C29). Scope: three Walkers runs, not OBVR's
  five-people scene.

## Candidate levers

Each with its ceiling (the share it could remove at most, from the table
above), the evidence, the risk and the cost.

| # | Lever | Ceiling and evidence | Risk | Cost |
|---|---|---|---|---|
| L1 | All streams of a partition in one lock/unlock | The D3D Lock/Unlock share, about 0.3-0.5 % of a pass; the bytes copied stay the same. Each stream has its own chip and buffer (read, 0x0076FE20.md C5/C8), so one lock needs one buffer for all streams: a layout change | the chip and ring semantics (owner slots unread), DXVK's handling of a bigger lock (unverified) | **medium**: a new allocation path behind the owner, the record's chips rewritten; bit-for-bit compare no longer possible (layout differs), only a rendered-image check. Not worth it |
| L2 | One pack per geometry per frame across passes (StreamReuse in OBNG) | **measured as nothing** by OBVR: 92 % skipped, no gain (spec lines 735-746); the second pass's packing about 0.14 ms a frame | eye-dependent records (OBVR's per-record verdicts) | **large** (rings enlarged, discard generations, check mode); OBVR has it built and off. Do not rebuild |
| L3 | Keep packed streams across frames (pack once, rebind until the source changes) | the whole PackStream chain, 8.0-11.6 % of a pass (about 0.37 ms in run 122650), plus the chip reallocation. Rests on one **unmeasured** fact: whether the bytes a volatile partition packs change from frame to frame. Under GPU skinning they would not (PackStream runs no CPU deform on the partition path, read, 0x0076FE20.md C9; bone palettes go to the shader as constants, knowledge/bone-palettes.md); morphing faces would | a cached body that does change (face morphs, equipment, LOD, a CPU deform elsewhere) shows stale vertices; device reset; the owner/ring protocol | **large**: own static buffers behind ports, an invalidation key (source pointers plus a hash of the source arrays, or the dirty bits), the owner slots read first, a rendered-image check besides the byte compare; headset look needed |
| L4 | Precomputed per-declaration plan instead of the per-entry dispatch | the dispatch's own code, 0.25-0.45 % of a pass (about 0.013 ms) | none beyond the rebuild's | **small** inside a rebuild (the plan is a table over the declaration's entries), but no gain anyone could measure. Comes free with L6, not worth doing alone |
| L5 | No staging copy: pack straight into the mapped pointer | the staging memcpy, 2.8-4.0 % of a pass (about 0.13 ms in run 122650), minus whatever the scattered writes into mapped memory cost instead. The engine already has the path: PackStream's arg7 != 0 skips the lock and writes to the caller's pointer (read, 0x0076FE20.md B7/C6) | the arg7 path also skips chip validation and reallocation (B7 jumps past B8-B13), so the caller must do them; without `cachedWriteOnlyBuffers` the mapped memory can be BAR memory, where OBVR measured a sequential copy at 1.2 ms a pass (spec lines 612-622): scattered writes there are unmeasured and could be worse; bytes between elements keep the mapped buffer's old content instead of staging garbage (OBVR saw such gaps, spec section 5); a lock without 0x1000/0x2000 reads back first (read, 0x0076FE20.md C7; flags in play unverified); the manager's critical section is entered by the wrapper (other users unverified) | **medium**: rebuild of 0x00763130's stream loop with the lock, the critical-section bookkeeping and the reallocation as ports; byte compare on element bytes only; a dxvk.conf dependency to document |
| L6 | Fast 0x0076F3B0 (blend indices to UBYTE4) | 1.3-1.5 % of a pass here, 2.5 % in OBVR's five-people run (spec line 560). Pseudocode (unverified) copies 1-4 bytes per element byte by byte and calls memset for the padding, once per vertex: the same per-vertex call pattern the fast copy removed | low: a leaf with one descriptor in, integer only (no float in its pseudocode) | **cheap**: the FastStreamCopy pattern (OBVR's StreamCopyLogic.h) reused; emulator gate on the leaf; in-game compare like StreamCopyCheck. The gain (about 0.05 ms a pass) is below what the Walkers series has resolved so far |
| L7 | Nothing | - | - | none |

The 1:1 rebuild itself (the precondition of L3, L4, L5 and the method
proof) is **medium**: four mapped bodies (228 + 239 + 209 + 176
instructions), about 30 ports, every branch of four flow tables (B1-B27,
B1-B32, B1-B18, B1-B17 in the function files) as unit flows, emulator
cases per function, a new shadow detour pair. What exists and is reused:
the function maps, OBVR's StreamCopyDesc layout and fast copy logic
(`../OBVR/src/render/StreamCopyLogic.h`, read), the emulator and its
traced stubs, the shadow and route framework of the first subsystem
(`plugin/src/ShadowRangeWalk*.{h,cpp}`, `RouteRangeWalk*`), the fixed
Walkers save. Routed 1:1, it removes nothing but part of L4: expected gain
**none measurable** (derived from the table: the dispatch is under 0.5 %).

## Verification plan

1. **Rebuild 1:1, leaves first** (64-bit clean: engine objects as
   `std::uint32_t` handles behind ports, offsets as `static_assert`ed
   constants, the bytes as spans):
   1. CopyVertexStreamElements 0x0076E610 (pure; OBVR's reference is the
      starting point, its zero-fill size corrected, 0x0076E610.md claim 18)
      and 0x0076F3B0, 0x0076EEB0 and the other leaves the probe in open
      item 4 finds in use;
   2. PackEntry 0x0076F590 (pure: selector table, descriptor, routine
      choice; the routine table and the size table as ports);
   3. PackStream 0x0076FE20 over ports (decl slots `+0x60/+0x68/+0x70`,
      geometry slot `+0x50`, 0x007780D0, 0x00776C40, 0x00761AC0, GetDesc
      `vt+0x34`, lock 0x00776C90, unlock 0x00776D80, 0x0072AF20);
   4. PackSkinnedGeometryBuffer 0x00763130 over ports (0x00777F10,
      0x00777F70, owner `vt+0x1C`, decl `vt+0x6C` = step 3, 0x007774C0,
      0x007781F0, 0x00777F40).
   Each step is **reconstructed** when its flows are green.
2. **Emulator gate** (`tools/difftest/emulate.py`, cases in
   `tools/difftest/cases/<address>.json`): per function the exported bytes
   against the rebuild, every write recorded, every callee a traced stub
   (virtual slots pointed at stubs). The memory image must carry the run-time
   tables: 0x00B42708 and `[0x00B42700]+4` are zero in the image (read,
   0x0076F590.md claim 10), so the cases write them (values from an
   instruction read of 0x0076F9B0 / 0x00771300 first, open item 3). The
   recorded writes double as the **coverage check** (which destination bytes
   each entry writes), the one thing the in-game compare cannot see.
   Control words: none of the five bodies has an x87 instruction (read:
   no x87 mnemonic in the asm.s of 0x0077A310, 0x00763130, 0x0076FE20,
   0x0076F590, 0x0076E610), so their cases need no `fpcw`. The colour
   leaf 0x0076EEB0 rounds `x * 255.0` to an integer (pseudocode, unverified):
   its cases run under all four rounding controls (0x007F, 0x047F, 0x087F,
   0x0C7F) with ties and out-of-range colours, per docs/re-workflow.md,
   until the control word at 0x0077A310's entry is measured (the existing
   `[Probe] FpuControlWord`).
3. **In-game shadow, both orders**, Oblivion Frame Fixes' `FastStreamCopy`
   off for the gate runs (it holds 0x0076E610; knowledge/culling-and-draw-
   setup.md, CopyVertexStreamElements line). The compare point is the
   **unlock**: when 0x00776D80 is entered, `[mgr+0x40]` holds exactly the
   bytes the original packed for that stream and `[mgr+0x4C]` their size
   (read, 0x0076FE20.md C6). The rebuild packs the same stream through its
   own PackStream with `lockedDest` = a host scratch buffer (the arg7 path,
   no D3D lock), on a copy of the record. So the shadow adds **no lock or
   unlock**, only one more CPU pack into host memory; the original's lock
   count stays 135 a pass. Compared bit for bit: the element bytes the
   rebuild's coverage says each entry writes (gaps excluded, OBVR found
   staging garbage there, spec section 5), the record fields written
   (format key, counts, stride, `+0x10`, `+0x0C`), the reallocation
   decision per stream (a port call recorded, not executed: a second
   allocation would consume a ring slice), AL, and the index bytes at the
   index pack's own lock.
   - original-first: the engine packs, the shadow snapshots staging at the
     unlock, the rebuild packs from the same source arrays (PackEntry stores
     only to its frame, read, 0x0076F590.md claim 22; the copy writes only
     the destination, read, 0x0076E610.md claim 15; PackStream's stores
     listed in 0x0076FE20.md go to the record, its frame and the chip slot;
     the other leaves unverified);
   - rebuild-first: the rebuild packs into scratch on the untouched state,
     then the engine; this order exposes any input the engine's pack
     changes (the lesson of the first subsystem, its "Verified" section).
   A rebuild is **verified** only when both orders run clean.
4. **Route** (`[Route] StreamPack=1`, a detour on 0x00763130 calling the
   rebuild with the real ports), only after 3, with the shadow's counters
   as the harness's expect lines.
5. **Fixed-scene measurement**: `tools/hand-scripts/obng-walkers-original.txt`
   / `-routed.txt` from `OBVRHarnessWalkers`, three runs per side
   alternating, `tools/measure-summary.py` over the phase "settled to
   watched", plus the sampler split above per run. Before any lever is
   judged, the original side alone shows the fixed scene's run-to-run
   spread of scene_pass (not measured yet: no `obng-walkers-*` run exists
   under `artifacts/shadow/` today); a lever smaller than that spread is
   not decidable in this scenario.

## Recommendation

**Do not build the stream-packing replacement now; run one probe first.**

- The best next step is a **cross-frame probe (cheap)**: at 0x00763130's
  entry, per partition, log `data+0x2E`'s nibble, the source pointers
  (`data+0x1C/+0x20/+0x24`, partition `+0x08/+0x10`) and a hash of the
  bytes each stream packs (taken at the unlock), keyed by record, frame and
  pass; over the Walkers scene report the share of packs whose hash equals
  the record's previous frame. It reuses OBNG.dll's probe framework and the
  unlock detour the shadow needs anyway; no rebuild. It settles the only
  lever with a ceiling above a few percent (L3, up to 8-12 % of a pass in
  these runs). If the bytes do not change for most bodies, L3 is the
  subsystem and the 1:1 rebuild (medium) is its first stage, with L5
  folded in; if they do change, the subsystem has no measurable lever
  left and should not be built for performance.
- What this gains over building the 1:1 rebuild straight away: the rebuild
  alone is medium work for a gain we already know is under 0.5 % of a pass
  (L4); over L6 (cheap, about 1.5 %): L6 is real but below what the
  scenario has resolved so far, and it is a leaf patch that belongs beside
  OBVR's and Frame Fixes' fast copy as much as in OBNG; over L1/L2: one is
  under 0.5 % with a layout change, the other is measured as nothing.
- If a second method proof is wanted regardless of gain, the 1:1 rebuild
  of PackEntry + PackStream with the unlock-compare shadow is the right
  piece: integer only, a clean compare point, no x87 traps except the
  colour leaf. Say so when starting it; do not sell it as a speed-up.

## Findings for OBVR and the knowledge files

- `perf-functions.py` counts 0x0077A310 as 0x0077A1F0 (no Ghidra start
  for 0x0077A310 in the function table): the "SetupShaderPrograms 49 %"
  of spec section 4A and knowledge/culling-and-draw-setup.md item 5 holds
  PrepareGeometryForRendering and the partition packing (14.3 of 27.3 % in
  run 122650). Adding 0x0077A310 to the table fixes it.
- The packing outside 0x00763130 that StreamReuse could not see runs
  through 0x0077A310 -> 0x00767B40 in these runs, not through 0x00771790 /
  0x00773330 (samples above; OBVR's five-people scene not re-checked).
- 0x0076F3B0 is, by pseudocode, the blend-index (UBYTE4) routine with a
  per-vertex memset for the padding, not a "memset loop" (unverified).

## Open

1. **Do the packed bytes change frame to frame?** (L3.) Cheapest: the
   probe in the recommendation.
2. **Who sets the 0x8000 nibble, and do skinned bodies carry it?**
   (0x0077A310.md C7; 0x007693E0.md C19.) Cheapest: the same probe logs
   `data+0x2E` at 0x00763130; the writer by a search for stores of 0x8000
   into `+0x2E` in `re/export/rea-all/functions/` (grep, then one
   instruction read).
3. **The routine and size tables** (0x00B42700, 0x00B42708): filled by
   0x0076F9B0 / 0x00771300, pseudocode only (0x0076F590.md claims 10-13).
   Cheapest: an instruction read of 0x0076F9B0's stores, or one dump of
   both tables from the running game by the probe.
4. **Which (destType, column) routines occur in play**, i.e. which leaves
   the rebuild needs. Cheapest: count `[[row+8]+column*4]` targets at
   PackEntry's call 0x0076F881 in the probe.
5. **The dirty groups**: selectors 1/2 and those outside 0..14 are packed
   unfiltered, 0 against bit 0x1, 3/13/14 against 0x2, 4 against 0x4,
   5..12 against 0x8 (read, 0x0076FE20.md C3); what sets the low 12 bits
   for mutable data, and whether bits above 0x8 mean anything besides 0x10
   / 0x20 (index refill) is unread. Cheapest: grep for ORs into `+0x2E`
   (0x00767B40 ORs 0x3 on a stamp mismatch, pseudocode, 0x0077A310.md C14).
6. **The record owner's slots `+0x18` / `+0x1C`** (0x0076FE20.md open 1):
   new ring slice or the same chip per volatile pack; decides L3 and L5.
   Cheapest: compare `record+0x24[i]` before and after one pack in the
   probe, then read the slot bodies.
7. **Lock flags in play** (`chip+0x10`; the read-back of 0x0076FE20.md C7):
   cheapest: the probe logs 0x00776C90's fourth argument.
8. **Streams per declaration and partitions per body** (the 135 unlocks
   split; 0x0076FE20.md C15): the probe's per-call counts.
9. **The fixed scene's run-to-run spread** (verification step 5): three
   original-side runs of `obng-walkers-original.txt`.
10. **The colour leaf's control word**: 0x0076EEB0 rounds floats
    (pseudocode); the FCW at 0x0077A310's entry from `[Probe]
    FpuControlWord` settles which modes its cases need.
11. **Gaps between elements**: whether the GPU ever reads bytes no
    declaration entry writes (decides whether L5's different gap content
    is harmless). Cheapest: the coverage from the emulator cases against
    the declaration's strides; the GPU side stays unverified without a
    rendered-image check.

## The cross-frame probe, run (2026-11-04 08:31)

`[Probe] PackCache=1` (Probes.cpp: the packer 0x00763130 keeps (geometry,
partition) and the data's consistency word per thread, the unlock
0x00776D80 hashes the staging bytes before the original copies them, keyed
by (geometry, partition, chip)), on the fixed Walkers scene with nothing
routed (`artifacts/shadow/obng-walkers-probes/20261104-083110`, 4496 frames
in the 60 s watched, medians of the steady windows, per frame):

| What | Per frame |
|---|---:|
| packer entries with static data (early-out, nothing packed) | 1 610 |
| packer entries with volatile data (0x8000), packed every time | 14 |
| packer entries with mutable data | 4 |
| packs (unlocks inside a packer call) | 14.1 |
| packs byte-identical to the key's last pack | **71 %** (by count and by bytes) |
| bytes packed | **4.15 MB** |
| distinct keys over the run | 35 (0 evicted, 0 new after the warm-up) |
| unlocks outside the packer (index and other buffers) | 52 |

So the scene packs fourteen large volatile streams a frame, 0.3 MB each,
and seven in ten of them carry exactly the bytes of the previous pack of the
same stream. Lever L3's ceiling on this scene is 71 % of the volatile
packing - of the 1.00 ms a frame measured for slot 15 (knowledge/
culling-and-draw-setup.md, round two), the part spent in these fourteen
packs; the 1 610 static early-outs and the binds are the rest (not split).
**Derived, unverified:** the fourteen are the Walkers' skinned bodies
(eighteen bodies, 35 keys); that they are marked volatile while their
packed bytes rarely change says the engine repacks on a flag, not on a
change.

What L3 needs before it is built (open from this run, both read questions):

1. **A change signal for the source.** The probe saw the packed bytes
   repeat; the rebuild must know *before* packing that they will. Either
   the engine has a writer of the volatile geometry's arrays with a version
   or dirty mark (unread), or the cache hashes the source arrays at the
   packer's entry (about the read cost of the pack, half its write cost).
2. **The chip's lifetime.** The key's chip pointer was stable (35 keys, 0
   new) but whether the D3D buffer range the unlock copied into survives
   until the next pack - the ring manager 0x007829A0 hands out slices and
   wraps (knowledge/skinned-vertex-streams.md) - decides whether "rebind
   the old bytes" is possible at all, or whether the cache can only skip the
   copier and must still refill the ring (the staging memcpy stays).

## The volatile packing, settled (2026-11-04 09:13)

Five more probe runs (`artifacts/shadow/obng-walkers-probes/20261104-084023`
to `-091327`, the probe keyed by stream and declaration, the volatile packer
calls timed without the probe's own hashing) and the packing-lifetime slice
(`re/functions/0x007829A0.md`, `0x00776C90.md`, `0x00776D80.md`,
`0x007774C0.md`, `0x00777F10.md`):

- The fixed scene's volatile packing is **one stream of one skinned
  partition** (geometry 1FA945B4 / partition 3905E6F4 in the last run; the
  pointers differ per load), packed once per pass that draws it: fourteen
  times a frame, under two declarations - 4 packs a frame under one layout,
  always the same bytes (38 384 same, 5 changed over the run), 10 under the
  other, 6 same and 4 changed a frame. The eighteen Walkers' bodies are
  static data: packed once, then the early-out 1 610 times a frame.
- **Measured cost: 36 us a pack, 0.51 ms a frame** for the fourteen (the
  probe's hashing of the 0.3 MB staging block costs 103 us a pack and was
  subtracted). That is the whole ceiling of lever L3 on this scene, 3.8 % of
  the 13.3 ms frame.
- Every pack reallocates its chip (0x007774C0 excludes 0x8000 data from the
  reuse path), reserves from a stride-keyed ring at offset 0 and locks with
  D3DLOCK_DISCARD (the ring's wrap rule, 0x007829A0.md); the slice never
  moved (buffer, offset, size the same on every pack), one buffer per chip
  (35 keys, 35 buffers). So the GPU-side bytes are renamed on every pack;
  a cache that skips the pack keeps the last content only while nothing
  else reserves from that ring (C13 of 0x007829A0.md). OBVR's StreamReuse
  never engages here for that reason (run `obng-walkers-reuse/20261104-085236`:
  "buffer DISCARDs 643, skipped 0 %"; scene_pass 2.50 against 2.45 ms).
- The 4 "changed" packs a frame under the second layout are **unverified**:
  the likely cause (derived from OBVR's "staging garbage in the gaps", spec
  section 5) is a partial dirty mask leaving stale staging bytes, not a
  changed source. A cache keyed by (record, declaration, dirty mask) once
  per frame would skip about 11 of the 14 packs: **about 0.4 ms a frame**.

Verdict: lever L3 is real and small. Building it is medium work (a route on
0x00763130 that owns the volatile records' chips per declaration, skips the
reallocation, lock and copy within a frame, verified by the unlock-bytes
shadow in both orders) for 0.4 ms of a frame that is held at 13.3 ms by the
pose wait. It is not built; the number is on record for the day the frame
is CPU-bound.
