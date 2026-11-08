# The occlusion culler (prototype, 2026-11-08)

What it is, what it rests on, and what the runs say. Marks as in the
knowledge files: **read** (the binary, with the address), **measured** (a
run, named), **derived** (from which), **unverified**.

## Why

The occlusion probe ([Probe] Occlusion, OcclusionProbe.cpp; measured
2026-11-05, knowledge/culling-and-draw-setup.md) found that in an eye pass
50 to 60 % of the testable draws have a world bound wholly behind the pass's
finished depth buffer, 33 to 39 % of the time in the draw entries; 50 to 73 %
of the draws put no pixel down. The engine culls against the frustum only
(0x0070DFB0). At Nadi's heavy Unique Landscapes / Better Cities place the
single-pass scene pass holds 13.3 ms of a 25.3 ms frame (OBVR capture
1-31, 2026-11-07, Windows), so the draws that put nothing down are the
largest CPU lever left in the DLLs (the constant uploads were measured at
30 ns a call, the per-draw state setup is the engine's own code).

## What it does (plugin/src/OcclusionCull.cpp, OcclusionCullLogic.h)

- Two detours: the accumulator's finish 0x007AE070, whose call from
  ShadowSceneNode::Render 0x007C78D0 (return site 0x007C7D56, read) is the
  eye pass, and Do_RenderShape 0x007693E0, the shape draw entry. The stereo
  route owns the direct entries (slots 90/91) and asks the culler before
  each direct draw (OcclusionCullBegin / End).
- Needs the single pass: the route's eye data (StereoEyeData) gives the
  second eye's offset and both viewports. Without it the pass runs untouched
  and is counted ("without eye data").
- Per draw inside the eye pass, at eye 0's viewport (one GetViewport, the
  route's gate): the geometry's world bound (+0x20) is projected into both
  eyes with the probe's BoundRect (eye 1's view = eye 0's with the
  translation row shifted by the offset, StereoEyeSwitchLogic.h), the
  rectangles grown by OcclusionCullMargin pixels. A bound that reaches the
  near plane, has no radius, or meets a port without area in either eye is
  untestable: drawn, never tested. A bound outside both frustums is not
  drawn (no test). Otherwise the verdict table is asked.
- The verdict table (16384 slots, open addressing, keyed by the geometry
  handle): a geometry whose last hidden result is at most
  OcclusionCullMaxAge frames old is skipped - unless the camera moved more
  than OcclusionCullMaxMove units or turned more than OcclusionCullMaxTurn
  degrees since that result's frame (the camera of every frame is kept in a
  ring). The first time a geometry is asked in a frame decides for its
  later passes in the same frame (repeats).
- Every geometry asked about (skipped or not) gets one test per frame: after
  the finish, one state block capture, the test state (depth test on,
  nothing written), one DrawPrimitiveUP of its one or two rectangles at the
  bound's nearest depth inside an occlusion query, the state block applied.
  Four batches of 4096 tests are in flight at most; the results are read at
  the start of the next eye passes without waiting (D3DGETDATA_FLUSH, never
  a stall); a batch whose slot is needed again before its results came is
  lost (counted).
- Check mode (OcclusionCullCheck=1): a skipped geometry is drawn anyway
  inside a query of its own; the summary says how many of the skipped draws
  put pixels down and how many pixels. That is the popping, measured.
- Soundness for a still camera (derived from the probe's measure, zero
  contradictions in 83 905 hidden bounds): a hidden verdict for X means
  every pixel of X's rectangle holds a depth nearer than X's nearest point,
  written by geometry other than X; whatever X hid lies behind those same
  pixels, so leaving X out uncovers nothing. A moving camera breaks it by
  one frame's motion; the margin and the gate bound that, the check mode
  measures the rest.
- A prototype: the queries and the state block are never released (a
  device reset with the culler on fails, as with the probe). Off in play
  until measured.

## The runs

Scenario tools/hand-scripts/obng-cull.txt: the probe's five places (Market
District, Talos Plaza, the Imperial Prison's first level, Weynon Priory's
house, the Gold Road at -27 -3), eight head directions for 2.5 s apiece,
single pass, OBVR's capture with GPU timing. Three sides:
obng-measure-cull-off.ini (the route alone), -on.ini, -check.ini. Summed per
place with tools/cull-summary.py.

Linux harness (gamescope headless, 75 Hz vsync, 2568 px an eye, MSAA 4,
save OBVRHarnessMOO, DXVK), OBNG.dll 1a0a8185 (off / on), 92513e63 (on,
second run, with the skip confirmation), OBVR.dll of the morning's deploy.
Runs: obng-cull/20261108-150835 (off), 151254 (on), 153720 (on, second),
obng-cull-late/20261108-152816 (off), 153234 (on). All measured.

| place | draws a pass | skipped | scene_pass off -> on, mean / p95 | GPU before submit off -> on, mean / p95 | culler's own cost a pass | skips unconfirmed |
|---|---:|---:|---|---|---:|---:|
| Market District | 332 | 27 % | 4.43 / 6.91 -> 3.71 / 5.77 ms (second on run 3.71 / 6.57) | 6.50 / 8.97 -> 5.96 / 8.08 ms (5.90 / 8.82) | 0.13 ms | 0.1 % |
| Talos Plaza | 240 | 44 % | 2.04 / 3.88 -> 1.56 / 2.56 (1.60 / 2.53) | 4.27 / 5.84 -> 4.02 / 5.00 (4.10 / 5.09) | 0.10 ms | 0.0 % |
| Imperial Dungeon | 438 | 41 % | 2.16 / 3.88 -> 1.60 / 2.26 (1.58 / 2.23) | 4.30 / 5.53 -> 4.11 / 4.71 (4.06 / 4.66) | 0.15 ms | 0.0 % |
| Weynon Priory house | 504 | 45 % | 2.61 / 6.85 -> 1.64 / 3.01 | 4.96 / 8.52 -> 4.23 / 5.31 | 0.13 ms | 0.0 % |
| Gold Road -27 -3 | 574 | 51 % | 3.93 / 4.73 -> 3.44 / 4.45 | 7.74 / 9.44 -> 6.55 / 7.29 | 0.28 ms | 0.1 % |

- **The scene pass falls by 12 to 37 % of its mean, the GPU's work by 4 to
  15 %** (measured, single runs per side; the two on runs agree within
  0.05 ms at the three places both cover, so the spread is below the
  effect).
- **The Gold Road held vsync only with the culler**: the off run drew 1 340
  frames in the place's 20 s at 14.92 ms mean with 12.9 % of the run's
  frames reprojected; the on run 1 496 frames at 13.37 ms (measured; one
  run each).
- **The skips were right**: of 174 443 to 581 947 skips per place, 0.0 to
  0.1 % were left unconfirmed by the same pass's test, the rest found their
  bound hidden in the frame they were skipped from (measured, second on
  run; an upper bound of the popping, with 45-degree head jumps every
  2.5 s in the scenario). The camera gate refused about 230 verdicts per
  10 s window, no batch was lost, every result came in time.
- **What was skipped**: on the Gold Road 574 draws a pass, 294 skipped -
  shapes and direct strips (terrain, trees) alike; 37 draws a pass were
  untestable (bounds reaching the near plane: the sky's followers, the
  player's body, the ground block under the camera).
- **Where the time goes**: the culler's own work is 0.10 to 0.28 ms a pass
  (decide 0.05 to 0.07, the end tests 0.06 to 0.12, collect 0.04 to 0.08),
  a tenth to a fifth of what it saves.

Derived for Nadi's heavy Windows place (13.3 ms scene pass, 15 to 19 ms
GPU, 2026-11-07, Better Cities and Unique Landscapes): at the Linux shares
the culler would take 2 to 4 ms off the scene pass and 1 to 2.5 ms off the
GPU. Not measured there; the Windows run with [Route] OcclusionCull=1 in
Nadi's OBNG.ini decides it.

## The pass-record skip and the Reset hook (2026-11-08, run obng-cull/20261108-174227)

DrawRenderPass 0x007A9820 detoured as well: a hidden record returns after
publishing the pass id and itself, before its light constants (steps 3 and 4
of re/functions/0x007A9820.md); a geometry asked about before in the frame
gets the frame's decision without the viewport read and the rectangles.
The device's Reset (vtable slot 16) is hooked at the first eye pass and
releases the queries and the state block before the engine's Reset.

Measured against the two earlier on runs (eye pass ms a pass: the finish
with the culler inside):

| place | eye pass before | eye pass with the record skip | culler's own cost before / after |
|---|---:|---:|---:|
| Market District | 1.223 / 1.274 | 1.207 | 0.135 / 0.139 |
| Talos Plaza | 0.630 / 0.635 | 0.614 | 0.101 / 0.105 |
| Imperial Dungeon | 0.972 / 0.959 | 0.899 | 0.147 / 0.123 |
| Weynon Priory house | 0.890 / 0.886 | 0.812 | 0.136 / 0.096 |
| Gold Road | 1.398 / 1.382 | 1.356 | 0.284 / 0.291 |

0.02 to 0.08 ms a pass less (2 to 8 %), the skips unchanged (0.0 to 0.1 %
unconfirmed), no crash, "Route: occlusion cull - Reset hooked on the
device's vtable" in the log. The run saw no device reset, so the release
path ran only in the unit tests (Ring::DropAll); a reset in play is the
test it still needs. In that run the shape asked at the record and again at
Do_RenderShape was counted twice (draws a pass 520 instead of 332 at the
market); fixed afterwards, the behaviour unchanged.

## What failed on the way

- A check mode that drew every skipped shape once more after the pass's
  last draw, inside a query, to count its visible pixels, crashed the game
  at 0x007FB4F2 reading a null record (run obng-cull/20261108-152718):
  Do_RenderShape needs the pass's record state, which the retired lists no
  longer hold. The first check mode (a query around the skipped draw at its
  own place in the pass) counted 8 to 52 % "with pixels", which are the
  pixels later draws cover (the probe's hidden-at-the-end bounds that still
  drew) - not popping. Both are gone; the same pass's test confirms a skip
  without any extra draw.

## Open

- The Reset path is hooked but not yet seen in a run (no reset happened):
  a run that forces one (alt-tab in windowed mode, a resolution change)
  settles it.
- The dual pass gets nothing (no eye data): a per-target verdict table would
  serve it (medium; off Nadi's path, the single pass is live).
- The margin and the camera gate were not varied: 2 px and 100 units / 10
  degrees held with 0.0 to 0.1 % unconfirmed; smaller values may skip
  more, a headset look decides what is visible.
- The 37 untestable draws a pass (near-plane bounds) cannot be culled by a
  bound; the player's body is OBVR's CullHiddenHead, the rest is the sky.

