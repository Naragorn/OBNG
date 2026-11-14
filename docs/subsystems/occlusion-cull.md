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

- ~~Open bug (Nadi, 2026-11-08 ~21:15, Windows headset): things pop, "alles
  blau manchmal"~~ **Found and fixed 2026-11-09, see the next section.**

## The blue geometry (2026-11-09): the first record of a pass list

Nadi's Windows headset (2026-11-08 ~21:15, OBNG.dll 006748a, single pass
with iMultiSample=0) showed popping and, at times, the whole picture blue;
the culler was turned off. Reproduced in the Linux harness the next morning
with the same DLL state, the eyes dumped at every mark
(tools/hand-scripts/obng-cull-texture-turn.txt: the Gold Road at -27 -3,
the head still, then turning at 90 and 180 degrees a second;
obng-cull-places-dump.txt: the Market District and the Imperial Prison, a
still look and a 90-degree turn). All measured, runs
artifacts/shadow/obng-cull-texture-turn/20261109-12* and
obng-cull-places-dump/20261109-12*/13*:

- Pure blue (0, 0, 255) silhouettes of the fallen log at the player's feet,
  rocks and a bush at the Gold Road still view (9-10 % of the eye), and of
  nearly every building wall when looking left in the Market District
  (69-75 %); the culler off: none. Alike on the image-space texture route
  (iMultiSample=0) and at MSAA 4, with the grass off (`tg`) and with it,
  and with two OBNG.dll builds (17d076eb and the day's). The Market case
  came and went between runs, the Gold Road case came every run.
- The summary's "skips unconfirmed" stayed at 0.0-0.1 % throughout: the
  metric cannot see this, because the blue things are **drawn**, not
  skipped (below). The Linux measurement runs of 2026-11-08 never had their
  pictures looked at; they carried the same blue.
- The trace ([Route] OcclusionCullTrace, below) at the Gold Road still
  view: the blue log "LandscapeLogScotchPine01" reaches the renderer every
  pass as one draw of pass id 185 with alpha blend off, alpha test off,
  depth write on and the colour mask 7 - a normal opaque draw that comes
  out blue. The depth buffer under the blue holds depth between 0.9 and
  0.99 (40 to 400 units): the blue is drawn geometry at the right depth
  with the wrong colour, not a hole. The culler's own camera, projection,
  surfaces, viewports and control rectangles (both eyes at depth 0: every
  pixel; at depth 1: the sky's share) were all as expected; the second
  queries at the far depth and at depth 0.5 found nothing wrong with the
  rectangles either.
- The pass-id trace put the log's siblings in view: in the passes where
  the Ayleid arches at the left edge of the view were skipped at their
  records (hidden behind trees, rightly), the first draw reaching the
  renderer in the pass-132 list changed from "ARArch03:1" to the log, and
  the pure blue followed the draws after a skipped list leader.
- **Read** (re/functions/0x007AC9A0.md steps 2 and 5): RenderPassList
  begins the pass's shader state with the first item's shader (0x007D1320),
  draws the list's FIRST record through DrawRenderPass, ends the state
  (0x007D1800), then draws the rest. With the first record left out, the
  begin and the end run with no draw between them, and the records drawn
  after it come out without their textures. What exactly the first draw
  does inside the bracket that the later ones need is unread (the shader
  slots 10/11/17 of the first item's pipeline are the candidates); the fix
  does not depend on it.
- **The fix** (ListTracker in OcclusionCullLogic.h): the first record of
  every pass list is drawn whatever its verdict. The lists come one pass
  id at a time, so a record whose pass id differs from the previous
  record's leads its list; CullDrawRenderPass draws it (and the
  Do_RenderShape under it) with g_forceDraw, counted in the summary as
  "hidden list leaders drawn" (1 a pass on the Gold Road, 2026-11-09
  13:53). **Measured:** the Gold Road and the Market/Prison scenario with
  the fix, every dumped frame 0.0 % pure blue (tools/cull-blue.py; runs
  obng-cull-texture-turn/20261109-135348, obng-cull-places-dump/20261109-135611);
  the Gold Road still skips 47 % of the draws, 270 of 224 852 skips
  unconfirmed.
- **Also fixed:** the end tests ran under the pass's last viewport, eye
  0's, which clipped every second-eye rectangle away - "hidden in both
  eyes" was eye 0 alone in every run so far. EndTests now sets one
  viewport over both ports (UnionViewport), the state block puts the old
  one back.
- **Also seen:** on the Gold Road the finish at the eye-pass site runs
  twice a frame - the second is the water reflection (all its draws
  outside eye 0's viewport, a 1024x1024 target, 6-13 draws); in the Market
  and the Prison once. The culler counts it as an eye pass, so there
  OcclusionCullMaxAge counts half frames.

### The A/B with the fix (2026-11-09, runs obng-cull/20261109-145453 off, -145901 on)

The five places again, OBNG.dll of master 8e97433 (the fix in), both eyes
tested. Skipped per pass: market 27 %, plaza 46 %, dungeon 42 %, house 45 %,
road 50 % - the same shares as on 2026-11-08 with eye 0 alone, so the
second eye's test takes next to nothing away; unconfirmed 0.0-0.1 % as
before. The culler's own cost 0.10-0.28 ms a pass as before. Frames: the
dungeon (indoors, no weather) scene_pass 2.21 / 4.08 -> 1.66 / 2.58 ms
mean / p95, the GPU before submit 4.39 / 5.78 -> 4.17 / 5.05 ms, the frame
13.41 -> 13.37 ms at vsync - the 2026-11-08 figures again. The outdoor
places of this pair are not comparable: the off run had rain or worse
weather (market 12 % and plaza 33 % of the frames over 20 ms, scene_pass
p95 20 and 12 ms, the GPU reading overflowed), the on run clear skies; the
capture ended before the house and the road. The scenario now forces clear
weather (fw 38EEE) for the next pair. All measured.

The clean pair (clear skies forced on both sides, OBNG.dll of master a29afc1,
runs obng-cull/20261109-152412 off and -152818 on), scene_pass mean / p95
and the GPU before submit mean / p95, both sides at vsync (13.3-13.5 ms):

| place | skipped | scene_pass off -> on | GPU off -> on |
|---|---:|---|---|
| Market District | 26 % | 3.98 / 5.77 -> 3.73 / 5.88 ms | 6.12 / 8.02 -> 6.15 / 8.30 |
| Talos Plaza | 44 % | 1.82 / 3.75 -> 1.72 / 2.95 | 4.12 / 5.65 -> 4.15 / 5.53 |
| Imperial Dungeon | 43 % | 2.08 / 3.76 -> 1.55 / 2.33 | 4.28 / 5.46 -> 4.02 / 4.70 |

The house and the road fell outside the capture on both sides (the loads
took longer than on 2026-11-08; 262144 events hold about 140 s). The on
side repeats 2026-11-08's on figures (market 3.71 -> 3.73, dungeon 1.60 ->
1.55); the off side came out lower than then at the market and the plaza
(4.43 -> 3.98, 2.04 -> 1.82), so the culler's gain outdoors is 0.1 to 0.7 ms
of scene pass depending on the weather and the run, indoors 0.5 ms
steadily. The GPU figure on Linux follows the submission and says nothing
here (as the dynamic-resolution runs found). All measured, one run a side.

## The flat game ([Route] OcclusionCullFlat, 2026-11-09)

OBNG is meant for every player one day, so the culler runs without OBVR's
eye data too: with OcclusionCullFlat=1 a pass whose render target has the
back buffer's size is the frame's one camera (FlatPorts: the target as the
port, eye 0 projected alone, RectsFor with one eye); a pass into another
target - the water's reflection map, the shadow maps, 1024 by 1024 - is
left alone (FlatTargetIsFrame, counted "into other targets"). Not for
OBVR's dual pass: two cameras a frame would share one verdict table.

Measured in the harness with OBVR drawing nothing to the headset
([Render] Enabled=0, Stereo=none - one scene pass a frame, the game drawn
flat into its window; the hand script needed OBVR's Present hook, which went
in only for the headset's submit before OBVR fa89e42), scenario
tools/hand-scripts/obng-cull-flat.txt, runs obng-cull-flat/20261109-172113
(off), -172519 (on, before the target check), -173055 (on, with it):

| place | draws a pass | skipped | pass ms with the culler | skips unconfirmed |
|---|---:|---:|---:|---:|
| Market District | 604 | 32 % | 1.29 | 0.0 % |
| Talos Plaza | 428 | 46 % | 0.73 | 0.0 % |
| Imperial Dungeon | 469 | 49 % | 0.57 | 0.0 % |
| Weynon Priory house | 475 | 57 % | 0.41 | 0.0 % |
| Gold Road | 1030 | 50 % | 1.49 | 0.1 % |

- The shares skipped are the single pass's (26-50 %); the pass takes about
  half the single pass's time, as one camera's draws should.
- Without the target check the reflection passes outdoors were culled
  with the frame's verdicts and vice versa: 5-10 % of the skips unconfirmed
  at the market, the plaza and the road, 0 % indoors; with it 0.0-0.1 %.
  Outdoors every frame has one such other-target pass (638 of each in a
  window).
- Every gamescope picture at the marks 0.0 % pure blue, on and off
  (tools/cull-blue.py --pattern '*-game.png').
- The flat pass without and with the skips is measured below
  ([Route] OcclusionCullAct, 2026-11-14); the frame time as a whole is not -
  OBVR's frame capture needs the headset renderer.

### What the skips save in the flat pass ([Route] OcclusionCullAct, 2026-11-14)

`OcclusionCullAct=0` tests and decides every draw as ever and draws it all
the same; `2` acts in every second summary window only
(OcclusionCullActLogic.h; the decision is turned into a draw in the table
too, the verdict stays as the tests left it, so an acting window finds it
warm). The eye pass's own clock then has the pass with the culler's work,
without and with its skips, under the same load. Run
obng-cull-flat/20261114-092219 (Linux, headless, OBNG.dll `067ebbd5`,
`tools/hand-scripts/obng-measure-cull-flat-act.ini`: the flat settings of
obng-measure-cull-flat-on.ini, a summary every second, the far LOD never
left out as it is since 2026-11-13), `tools/cull-summary.py`:

| place | draws a pass | left out, acting | pass ms without the skips | with them | saved | the culler's own ms |
|---|---:|---:|---:|---:|---:|---:|
| Market District | 616 | 14 % | 2.810 | 2.712 | 0.098 (3 %) | 0.25 |
| Talos Plaza | 325 / 362 | 33 % | 1.092 | 0.850 | 0.241 (22 %) | 0.14-0.16 |
| Imperial Dungeon | 434 | 38 % | 1.165 | 0.844 | 0.321 (28 %) | 0.13 |
| Weynon Priory house | 625 | 60 % | 1.360 | 0.656 | 0.704 (52 %) | 0.11 |
| Gold Road | 1051 / 1088 | 42 % | 2.447 | 1.990 | 0.457 (19 %) | 0.44-0.47 |

- **Measured**: the columns. Each place has about ten to fifteen windows of either kind
  (888 to 1437 passes); the head turns every 2.5 s, so the two kinds do
  not see exactly the same views - at the plaza the test-only windows had
  11 % more draws asked a pass (362 against 325), at the road 4 %, the
  others within 1 %. The plaza's saving is that much overstated.
- **Derived, not measured**: the pass with the culler off altogether. Both
  columns carry the culler's own work (decide, the tests, the collect); the
  pass without the culler is about the test-only pass less that work, and
  against it the culler gains at the house 0.59 ms (47 %), in the dungeon
  0.19 ms (18 %), at the plaza 0.08 ms (9 %), at the road nothing (-0.01)
  and at the market it costs 0.15 ms (6 %). The queries' cost on the GPU is
  in neither number, and a run with the culler off has no pass clock to
  check the subtraction against.
- So in the flat game, one camera, the culler pays where much is hidden
  behind walls and costs about what it saves in the open: half the single
  pass's gain a draw (one eye's draws instead of two) for the same tests.
  The market's 14 % left out is less than half of 2026-11-09's 32 % - the
  far LOD and the guards of 2026-11-13 came between; which of them was not
  taken apart here.
- Every gamescope picture at the marks 0.0 % pure blue.

### The trace ([Route] OcclusionCullTrace=N, OcclusionCullTraceDepth)

For such a hunt: every 75th eye pass, N times, the log gets the camera and
the planes, the render target, depth-stencil and viewport at the pass and
at the tests, every renderer draw's pass id in order with its draw count
and the alpha blend / alpha test / depth write / colour mask of its first
draw (OcclusionCullNoteDraw from the route's two entries), eight control
rectangles over both ports at depths 0, 0.5, 0.9, 0.99, 0.995, 0.997,
0.999 and 1 (a depth histogram of the buffer), and every hidden or skipped
test among the pass's first 512 with its name, parent node, bound,
rectangles, depth and pixel count, plus a second query of the same
rectangles at the bound's far depth (or at OcclusionCullTraceDepth). INI:
tools/hand-scripts/obng-measure-cull-trace.ini. Off by default, costs
nothing then.

## Nodes left out at the cull walk (2026-11-10, class C, `[Route] OcclusionCullNodes`)

Why: in Anvil 58 % of an eye pass's asks are hidden and 60-72 % of those
sit below a node all of whose draws are hidden (docs/performance-backlog.md,
"Three counts in Anvil"); the engine still walks to each of them, collects
it, and the culler decides and tests each.

What: two more detours. The eye pass's driver ShadowSceneNode::OnVisible
0x007C78D0 (re/functions/0x007C78D0.md: the cull walk, then the finish) -
the pass now begins at its entry (BeginEyePass, what the finish's detour
did before), so the walk's decisions have the frame's eyes, batch and
results. NiCullingProcess::Cull 0x0070DFB0, the visit of one object: in the
eye pass's walk a plain node or fade node (its +0x7C is 0x0070AB40 or
0x004A0920) that is a candidate is asked about like a draw - its world
bound projected, the verdict table keyed by the node, one test a frame -
and, hidden, not visited. That is what the engine does with a node outside
the frustum, so nothing that hangs on a node's visit is skipped that the
engine does not skip itself (derived from the read of 0x0070DFB0).

Candidates (DrawGroupLogic.h NodeBook, tested): every newly asked draw is
noted at its three nearest ancestors; a node whose draws of its last pass
(within the culler's age) were all hidden, two or more, is asked; a node
left out stays a candidate while it is being left out. So a node costs a
test only when it is likely to pay.

Measured (Anvil with the Windows mod list, one pair of runs,
obng-profile-anvil-bc/20261110-151142 off and -151529 on, both with
ConstantArrange): 31-58 nodes a pass left out of 40-68 asked; the asks
594 -> 444 a pass, the tests 462 -> 338, the culler's own time 0.29 ->
0.24 ms; the scene pass's median 3.33 -> 3.07 ms, the render thread's busy
median 6.19 -> 5.84 ms, its p90 8.28 -> 8.73 and p95 8.97 -> 9.52 (the
tail the wrong way in this pair - a second pair has to say whether that is
the runs or the change); unconfirmed skips 0.09 -> 0.10 %; the view never
differed between the walk and the finish; no crash.

The picture (tools/hand-scripts/obng-anvil-bc-eyedump.txt: the AI off, the
weather and the hour fixed; tools/eye-compare.py; runs obng-anvil-bc-
eyedump/20261110-153110 off and -153614 on): at the two fixed spots, 18
views and both eyes, 30 of 36 pictures differ in under 0.7 % of their
pixels and the other six in 2.3-4.3 %; the largest, looked at, is the
gate's guard standing a step away with his shadow, the rest of the mask
black (a picture impression for the one looked at, the numbers measured).
No pure blue in any dumped frame. The marks after a walk differ because
the walk ends elsewhere, and say nothing.

State: reconstructed and run; not accepted - the second pair, a long run
and the look in the headset are open.

### The second pair (2026-11-10, runs obng-profile-anvil-bc/20261110-160041 off, -160434 on)

| | pair 1 off | pair 1 on | pair 2 off | pair 2 on |
|---|---|---|---|---|
| scene pass, median | 3.33 ms | 3.07 | 3.80 | 2.88 |
| render thread busy, median / p90 | 6.19 / 8.28 | 5.84 / 8.73 | 6.89 / 9.16 | 6.11 / 8.72 |
| frames with busy over 12.5 ms | 142 | 142 | 200 | 258 |
| ... over 30 ms | 15 | 14 | 13 | 80 |

The median is better in both pairs (the scene pass by 0.26 and 0.92 ms; two
runs of the same side differ by 0.5 ms themselves, so the size is not
settled). The frames over 30 ms are the same in pair 1; pair 2's on run has
80, of which 74 lie in one stretch of 15 s (25-40 s into the capture, the
scene pass 17 ms in them, the walk's window at 3.0 ms a pass against 0.9-
1.3 elsewhere). What that stretch was is not known - the change, or
something beside the run; the other three runs have no such stretch. Open
until more runs or a long one show it again or not.

### Two more on runs and one off (2026-11-10, runs obng-profile-anvil-bc/20261110-165228 on, -165621 off, -170016 on)

The scene pass's median over all seven runs: on 3.07, 2.88, 2.98, 3.16 ms;
off 3.33, 3.80, 3.58 ms - 3.02 against 3.57 in the mean, 0.55 ms (15 %)
less with the nodes. Frames with the render thread busy over 30 ms: on 14,
80, 10, 9; off 15, 13, 11. The stretch of slow frames of the second on run
did not come back in two more; a run without the node cull and with other
settings showed a longer one (below), so it is not the node cull's.

These runs did not fix the game's weather and hour, which the save rolls
anew each run; two runs with identical DLLs and INIs differed by 2.4 ms a
frame outside the scene pass (docs/performance-backlog.md). The scene
pass's median moved less (the on runs lie within 0.28 ms, the off runs
within 0.47), but the pairs are weaker than they looked. With the weather
and hour fixed (the scenario since 17:08) a run with the nodes has the
scene pass at 2.68-2.71 ms; the off side at fixed weather is not run yet.

## The prison bars defect: a blended surface that writes depth (found and fixed 2026-11-13)

Nadi on Windows, 2026-11-11 and -12: behind the cell's bars in a new game's prison the floor was blue and NPCs lacked
body parts, fine with the head through the bars; gone with `OcclusionCull=0`. Reproduced on Linux with
`tools/hand-scripts/obng-newgame-bars-sway.txt` (from `tools/make-newgame-bars.py --inside 45`: a new game clicked
through, the player placed 45 units inside the cell from its door by the console, the head turned to the bars and
swayed, both eyes dumped at 40 marks) - earlier scenarios stood in the door or deep in the cell and showed nothing.

Measured (the left eye's dumps, pixels of the clear colour (18, 27, 27) in the right half):

| Run | Settings | Clear-colour pixels at four marks |
|---|---|---|
| 20261113-124456 | the routes as played, culler with nodes | 12 749, 12 933, 13 034, 0 |
| 20261113-130048 | culler without the nodes | 2 641, 3 089, 2 852, 2 523 |
| 20261113-124907 | culler off | 0, 0, 0, 0 |
| 20261113-131610 | culler with nodes, the tests before the first blended record | 0, 0, 0, 0 |

With the defect the cell opposite, its gate and Valen Dreth behind it were left out (the node log names them at the
frame the player was placed: `(0001FC43) -> Valen Dreth`, two `prisonCell02` nodes, the gate's `FadeNode Anim`), their
tests reading 0 pixels. The trace's control rectangles found **no depth beyond 0.99 anywhere in the picture** (0 pixels
at z 0.99 and above) although the picture showed the clear colour there: something had written depth without colour
over the doorway. With the fix the controls read 1 988 436 pixels at 0.99 and 600 114 at 0.995.

Cause (derived from those two readings; which mesh it is was not looked up): a blended surface with depth write in
the cell's doorway. The tests ran after the whole finish, the blended records included, so they took a see-through
surface for cover - once what stood behind it was left out, nothing drew there and the clear colour showed through.
The check inside the pass ("skips confirmed hidden") could not see it: it reads the same depth.

Fix (`[Route] OcclusionCullBeforeBlend`, on by default; `EndsTestsBefore` in OcclusionCullLogic.h with its test): the
pass's tests go out before the first record whose NiAlphaProperty has the blend bit (flags bit 0), so they read the
opaque geometry's depth alone. What is asked after that gets no test in the pass and therefore no hidden verdict. In
the prison the tests went out early in every pass (746-750 of 750 a window); 45 of 1 014 draws a pass were still left
out there. Not measured yet: what the culler still saves in a town with this (the blended records come late in the
finish, so most of the opaque depth is there), and Windows itself.

## The culler's own cost: the quick decision and the stand-down (2026-11-14, class C)

Nadi's Windows session of 2026-11-14 (docs/performance-backlog.md): over 200 347 eye passes the culler left out
127 of 968 draws a pass (13.1 %) and cost 0.278 ms a pass (decide 0.173, the tests' end 0.067, the collect 0.038);
at the mean cost of a drawn draw its skips saved less than that in 81 of 254 windows (29 % of the passes). In a
horde at Anvil: 3 983 draws a pass, 25 left out, 1.06 ms a pass.

### `[Route] OcclusionCullQuick` (default 1): no rectangles where none are needed

The decision read the renderer's view and projection, built the second eye's view and projected the bound into
both eyes for every draw of every frame. The rectangles serve a test of this frame and tell a bound outside both
frustums, nothing else. A draw whose verdict on record is a visible one for this very object (the identity) and
whose test is not due - `OcclusionCullVisibleEvery`, or a far LOD piece that is never tested - is drawn whatever
they say; `quick::Applies` / `quick::Decide` (OcclusionCullQuickLogic.h) decide it from the verdict alone, the
node notes and the counts as before.

- The one difference (class C): a bound outside both frustums is drawn where the full decision leaves it out.
- **The check in the game** (`OcclusionCullQuick=2`: the full decision taken for every draw, the quick one
  compared; runs obng-profile-gate-guards/20261114-151935 and obng-profile-gate-guards-half/20261114-152317, the
  horde at Anvil's gate, Linux): **3 036 197 quick decisions compared, all the same, 0 outside both frustums, none
  differing otherwise.** The tolerance the class C row needs: 0 of 3.0 million in a horde; a place where the
  engine's own cull lets bounds outside both frustums through was not looked for.
- **What it saves** (`OcclusionCullQuick=3`, alternating summary windows, run -150327, 10 windows each): decide
  0.719 -> 0.597 ms a pass at 1 598 first-time draws a pass, 879 of 1 643 decisions quick. A sixth of the decide,
  not the half the rectangles' share of the samples promised: what is left per draw (the viewport read from the
  device, the table's probe, the identity's reads, the notes at three ancestors, two clock reads) is 0.29 us
  against 0.45 us with the rectangles. That run's frames were 4.5 a second (the sound's wait under Proton,
  knowledge/voice-lines.md), so the memory was cold at every draw; on Windows the full decision was 0.18 us a draw.

### `[Route] OcclusionCullStandDown` (default 0): the culler rests where it does not pay

OcclusionCullStandDownLogic.h: windows of 32 passes; the saving is the draws left out, and those noted below a
node left out (`NodeBook::DrawsBelow`), at the mean cost of a draw that was drawn - (pass - culler) / drawn -
against decide + the tests' end + the collect. Two windows in a row that do not pay: a rest of 256 passes (every
draw undecided, no test, no node left out - the walk finds no candidate without the draws' notes), then four
passes of warm-up and a probe window that must save 1.25 times its cost; a probe that fails doubles the rest up to
2 048 passes. 2 = judged and counted only.

- **Measured in the horde** (Linux, the sound off, the quick decision on):
  - what the skips really save, by `OcclusionCullAct=2` (run -153926, 6 acting and 7 test-only windows of 2 283
    draws a pass): the eye pass 10.956 ms acting (114 left out a pass) against 11.518 ms test only - **0.56 ms a
    pass saved, the culler's own work 1.21 ms a pass: 0.65 ms a pass lost.**
  - the stand-down's own estimate in the same place (counted only, runs -151935 and -153926): 0.0 to 0.23 ms a pass
    saved - lower than the measured 0.56 (the hidden draws there are skinned and cost more than the mean), the same
    verdict. 33 of 132 and 7 of 130 windows paid.
  - on (run -154331): rested in 3 985 of 4 193 passes, 5 rests begun, no probe paid; the culler's line reads 0
    draws and 0.000 ms decide in the horde's windows. The eye pass without the culler was not measured against the
    one with it in one run (there is no alternating mode); derived from the run above it is 11.52 - 1.21 = 10.3 ms
    against 10.96 ms.
- While it rests the summary's "per pass N draws" counts the working passes' draws over all passes: read the
  stand-down line beside it.
- **Open**: a place where the culler pays (a dungeon, a house) with the stand-down on - that it does not rest
  there, and that the estimate's low side for skinned draws does not rest it where actors stand behind walls;
  Windows; the rest lengths were not varied.
