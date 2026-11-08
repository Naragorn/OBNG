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

(results follow)
