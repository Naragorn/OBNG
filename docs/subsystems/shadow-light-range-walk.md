# Shadow light range walk (replacement of 0x007D59E0)

OBNG's first subsystem replacement: the walk that collects, per shadow light,
the scene geometry within the light's reach. Pure logic with tests; its
in-game gate is OBNG.dll's `RangeWalk` shadow (`plugin/src/ShadowRangeWalk.cpp`,
flow in `plugin/src/ShadowRangeWalkLogic.h`). Code: `src/engine/render/ShadowLightRangeWalkLogic.h` (the two
walks), `src/engine/render/ShadowLightRangeWalk.h` (the engine ports), tests:
`tests/ShadowLightRangeWalkTest.cpp`. Marks: read / measured / derived /
unverified (AGENTS.md).

## Verified in the game (2026-10-30)

Run `artifacts/shadow/obng-range-walk/20261030-120624` (prison empty, then
eighteen Walkers; `plugin/src/ShadowRangeWalk.cpp`'s shadow detour): 74 573
walks compared - 30 996 on the plane path (only with the Walkers' actor-shadow
lights), 43 577 on the distance path -, 3 574 564 AddGeometry calls in
sequence with their masks, the final mask and AL per walk: **0 mismatches**.
That gate ran in the original-first order. The rebuild-first order (the
rebuild on the untouched scene, then the original) found two holes on
2026-10-30: the game runs this walk with the x87 rounding control set to
truncate in some runs (FCW 0x0C7F) where the rebuild assumed nearest - fixed
by making the mode an input, verified in the emulator under all four modes
(`tools/difftest/cases/0x007D2EA0-rounding.json`, `0x0047DA70-rounding.json`)
and in the game (run 13:53, 52 938 walks, 0 mismatches) -, and the first
rounding helper called the CRT's x87 math per store, which made the route
take 43 % of the render thread until it was replaced by bit arithmetic.

**Routed and measured** (`[Route] RangeWalk=1`, `plugin/src/RouteRangeWalk.cpp`,
flow in `plugin/src/RouteRangeWalkLogic.h`; series of three runs per side
14:24-14:48, `tools/measure-summary.py` over
`tools/hand-scripts/obng-range-walk-original.txt` / `-routed.txt`): frame
means with eighteen Walkers 13.62 / 13.64 / 13.81 ms original against
13.62 / 13.93 / 13.80 ms routed, scene_pass 0.25 / 0.71 / 1.02 ms against
3.41 / 0.23 / 0.25 ms - the scene's own run-to-run variance (random Walker
placement, 76 000 to 382 000 AddGeometry calls per 10 s between routed runs)
is larger than any difference between the sides. The route costs nothing
this scenario can measure and gains nothing it can measure; the subsystem is
done as the proof of the method, and the shipped OBNG.ini keeps the route
off in play until a block with a measurable gain carries it.

## The unit

One light, one walk: one call of `ShadowSceneLight::CollectGeometryInRange`
0x007D59E0 (proposed name) from its entry 0x007D6900, from the root object
down (read, `re/functions/0x007D59E0.md`). Per visited object one range test
0x007D2EA0; an out-of-range object's subtree is skipped; an in-range node
recurses (an exact NiSwitchNode only into its active child), an in-range
geometry goes to AddGeometry 0x007D5790. Measured: ~2.0-2.4 M visits per 10 s
with eighteen Walkers, and the plane path's ~10 M WhichSide calls per 10 s fit
coming through this walk (`knowledge/culling-and-draw-setup.md`, run
20261030-101556).

## The boundary

- Inputs: the gate [0x00B42F48]; the light's source (+0x100: null, its RTTI,
  its position +0x88 and range +0xF8), plane source (+0x14C), planes (+0x150),
  mask (+0x1B0); the scene tree below the root (node/geometry queries, the
  switch's active child, child arrays, flags +0x18, world bounds +0x20..+0x2C).
- Outputs: the sequence of AddGeometry calls (the geometry and the value
  +0x1B0 holds at the call); +0x1B0 afterwards (equal to its entry value,
  derived from C3); AL (0 only when the gate is off).
- Observed for the comparison: per visited object, in order, the in/out
  decision and the mask before and after its range test.

## What the batched version changes, and why it stays exact

| Change | Why it is exact | Mark |
|---|---|---|
| Plane test as one batch per object: distances of all active lanes in one pass, classification in a second, mask and cull derived by bit operations; no call per plane | each lane is WhichSide's own `SignedDistance` + `Classify` and IsInRange's `ClassifySide` - the same function, the same roundings, evaluated per lane | derived; differential |
| Mask read once per object instead of per plane | inside one test the only write clears the current plane's bit, later planes test higher bits, WhichSide writes no memory but its argument slot | derived from read (0x007D2F3E, B7; 0x0047DA70 mapping) |
| Explicit stack instead of native recursion; the mask travels on the stack, +0x1B0 never written | the original restores +0x1B0 on every exit; children see the parent's narrowed mask, siblings do not see each other's | derived from C3/C4; C4 rests on the helpers not writing +0x1B0 (unverified) |
| Source light read once per walk (null / directional / plane / distance chosen once) | the walk and IsInRange write only +0x1B0 | read for those two bodies; unverified for 0x007D5790, the virtual calls and the two helpers |

Kept on purpose: the number of per-plane classifications (one per active lane,
no early exit - the original does not exit early either, C6 of 0x007D2EA0);
the gate read per call, the child count and slot re-read per child, the double
vt[+0x10] call. Each of those could be dropped only after the callee that might
change the scene (0x007D5790) or return differently (vt[+0x10]) is read.

## What the engine ports must wrap

| Port | Engine operation | Status of the address/shape |
|---|---|---|
| the walk itself (hook site) | 0x007D59E0, `__thiscall(light; object)`, RET 4 | read |
| `addGeometry` | 0x007D5790, `__thiscall(light; geometry)` | call read; body unverified (knowledge/shadow-light-list-remove.md) |
| `activeChild` | 0x007C59E0, `__thiscall(switch)` | call read; body pseudocode only; single code path |
| `hasNonZeroBoundRadius` | 0x0049FFC0, `__thiscall(object)` -> AL | call read; body pseudocode only; single code path |
| `gate` | dword 0x00B42F48 | read; meaning unverified |
| `asNode` / `nodeRtti` / `asGeometry` | vtable slots +0x08 / +0x04 / +0x10 | read; names unverified |
| `childCount` / `child` / `flags` / `bound` | +0xB6 u16, +0xB0 array, +0x18 byte, +0x20..+0x2C | read |
| `LightValues` | +0x100, source GetRTTI, +0x14C, +0x150, +0x1B0, source +0x88/+0xF8 | read (0x007D2EA0 mapping) |
| square root | 0x00982C30 | CRT routine; rounding vs FSQRT unverified |

## How the in-game gate will compare the two

Per light per pass: run the original 0x007D59E0 (with a recording AddGeometry
hook) and BatchedWalk on the same light and root, then compare the AddGeometry
sequences element by element (geometry and live mask) and +0x1B0 afterwards;
optionally the per-visit decisions via a counter at the 0x007D5A12 branch. A
rebuild is **verified** only after that comparison has run clean in the game;
today it is **reconstructed** (tests pass; the oracle is the IsInRange rebuild
verified in the emulator).

## Still unverified

- 0x007C59E0 and 0x0049FFC0: their instructions are now read
  (re/export/range-callers/; signed index bounds against the u16 count, NaN
  counts as a non-zero radius, as B11); each still has one code path only, and
  the walk obeys whatever the port answers.
- +0x14C's meaning (only its zero-ness is used) and what sets +0x14C / +0x1B0
  before 0x007D6900 (0x007D6390, 0x007D5ED0 unread).
- 0x007D5790: whether the insertion order matters to its consumers, whether it
  reads +0x1B0 (the port gets the live mask in case it does), and whether it
  can change the scene or the gate (the walk keeps the re-reads in case it can).
- Whether vt[+0x10] returns `this` (would allow one call instead of two).
- No performance number is claimed; none has been measured for this rebuild.

## The fixed scene (2026-10-30)

The measured series above could not resolve the route's cost because the
scene itself varied from run to run (the Walkers placed anew each run, eight
seconds of AI before `tai`). The scenario is now a saved game:
`tools/hand-scripts/obng-make-walkers-save.txt` writes `OBVRHarnessWalkers.ess`
(ImperialDungeon01, eighteen Walkers round the held player, AI off, saved after
twenty seconds), and `obng-walkers-original.txt` / `obng-walkers-routed.txt`
load it through the harness's `--save` and watch it for sixty seconds under
OBVR's profiler (`tools/measure-summary.py` reads the phase "settled to
watched"). Two runs of the original side (15:17 and 15:20):

| Run | Frames | Frame mean | p95 | scene_pass mean | p95 |
|---|---:|---:|---:|---:|---:|
| 20261030-151727 | 4497 | 13.34 ms | 13.83 | 2.68 ms | 3.07 |
| 20261030-152017 | 4460 | 13.35 ms | 13.90 | 2.69 ms | 3.14 |

The scene pass agrees to 0.01 ms between runs (0.25 to 3.4 ms spread before),
and OBVR's relight counter reports the same 1680 / 360 geometries per 120 scene
calls in both. A lever of 0.1 ms is now resolvable with one run per side. Still
unverified: whether `tai`'s state survives the load (the scenario sends it again
one second after the load; a toggle that survived would turn the AI back on, and
the agreement above says the Walkers stood still either way, so the question is
open but harmless here). The routed side has not been run on the fixed scene.

## Routed on the fixed scene: slower (2026-10-30 15:28)

| Side | Run | Frames | scene_pass mean | p95 |
|---|---|---:|---:|---:|
| original | 20261030-151727 | 4497 | 2.68 ms | 3.07 |
| original | 20261030-152017 | 4460 | 2.69 ms | 3.14 |
| routed | 20261030-152849 | 4496 | 3.15 ms | 4.07 |

The route costs 0.46 ms a pass (+17 %), every walk routed (3004 walks per
10 s, all on the plane path, all at rounding control truncate). The series
of the morning could not see this: its scene varied by more than that.

Where the cost is (measured, OBVR's render sampler through
`../OBVR/tools/perf-functions.py`, phase settled to watched, in-pass
samples): OBNG.dll holds 14.1 % of the routed run's in-pass samples (3575 of
25 362; 99.4 % with the shadow draw 0x007D6900 on the stack), while the
engine's own walk 0x007D59E0 held 3.1 % inclusive (669 of 21 666) in the
original run: the rebuild runs about five times slower than the walk it
replaces. Read in the DLL (build c09df5173de6ae0a, `llvm-objdump -d` at
.text+0x54A0; the region +0x5480..+0x6180 holds over 90 % of the DLL's
samples): `cvtss2sd`, `mulsd`, then the exponent bit arithmetic of the
software 24-bit rounding. The cost is the bit-exact x87 emulation
(`MulPc24` / `AddPc24` / `RoundToPc24` in NiBound_WhichSideLogic.h), not the
batching and not the ports.

What follows (design, not built): a native path in IEEE single precision
(SSE) under the call site's rounding mode (MXCSR RC set from the FCW's RC),
which equals x87 PC=24 arithmetic on single operands except where an
intermediate leaves the single exponent range (x87 keeps a 15-bit exponent
until the store; the garbage-sized root bound of df7bb93 is such a case),
and the exact software path as the fallback whenever MXCSR's overflow,
underflow or denormal flags were raised during the walk. Verified the same
way as the exact path (the 30 000-tree differential, the emulator cases
under the four modes, the in-game shadow in both orders), then routed and
measured on the fixed scene. Until then the route stays off (OBNG.ini
[Route] RangeWalk=0, the play default). This is a finding about the
contract, not only about this subsystem: every future float rebuild takes
the native-with-fallback shape, software rounding is the oracle, never the
hot path.

## The native reading, built (2026-10-30 17:20)

Built as designed above: `SignedDistanceNative` and `NativeDistances` in
`src/engine/render/NiBound_WhichSideLogic.h` ("The native reading"), the MXCSR
port in `src/engine/render/Mxcsr.h`, `ClassifyPlanes` taking the port and
counting per walk how many objects the native pass settled and how many the
exact path recomputed (`PlaneTestCounters`, in the route's and the shadow's
summary lines as "plane batches native N, recomputed M"). The contract and
its evidence are in `knowledge/floating-point-contract.md`.

What the tests measured (all green, 2026-10-30 17:17):

| Test | What | Result |
|---|---|---|
| tests/NiBound_WhichSideNativeTest.cpp | the host's x87 running 0x0047DA74..0x0047DA97 against the exact path, the native path with its fallback, and the flags' soundness; 4 control words x 200 000 edge-biased inputs; ten forced cases (OE, UE, DE, IE, inf, the garbage bound) | 0 mismatches on both paths, 0 unreported raw differences; forced cases as predicted; 50 000 of 50 000 game-like inputs kept natively |
| tests/ShadowLightRangeWalkTest.cpp | the flows over a fake port (flags decide, the register put back, no lane: no access) and the 30 000-tree differential against ReferenceWalk through the exact IsInRange | 0 differences; 34 990 batches kept natively, 2 445 recomputed (the wild values) |
| plugin tests | the route and the shadow carry the port and the counters | green |
| tools/bench/range-walk-batch-bench.cpp | one six-lane batch on this host, clang++ -m32 -O2 | exact 314 ns, native with the MXCSR scope 13.4 ns (23 x) |

Not yet run (the game was held by OBVR's runs): the in-game shadow on the
fixed scene (tools/hand-scripts/obng-walkers-check.txt, original-first,
expecting 0 mismatches and the native share in the summary line), then the
routed measurement against the original on the same scene with the same
OBVR.dll (the OBVR.dll changed at 17:14, so the original side is re-run too).
