# The frame clock

Status: knowledge, written 2026-10-25 from OBVR (`../OBVR`,
`src/game/FrameClockLogic.h`, `src/game/FrameClock.cpp`,
docs/performance-spec.md section 4 C). OBVR has built the fix described
below as an engine detour. Nothing of it is built in OBNG.

Marks: **read** = read in Oblivion.exe 1.2.0.416 (Ghidra/REA),
**measured** = measured in the game, **derived** = follows from the two,
**unverified** = not checked.

## How Oblivion measures a frame (read)

- The main loop calls `GetTickCount` once a frame at 0x0040D89F. It hands
  the tick to the time info's update, 0x0047D170. That update is a thiscall
  on the `TimeInfo` at 0x00B33E90, which xOBSE calls `g_timeInfo`. It
  returns with ret 4.
- The update takes the frame's length as an **integer number of
  milliseconds**: the tick minus the previous frame's tick.
  - A zero reads as 1 ms.
  - Anything over 166.67 ms reads as 166. The 166.67 is the float at
    0x00A3D14C (1000/6), and 166 is the immediate 0xA6 it clamps to.
- It stores `ms * 0.001 * multiplier` as the frame's seconds at +0x0C. The
  multiplier is the global time multiplier at 0x00B06704, applied by
  `fmul [0x00B06704]` at 0x0047D247. Every time-driven update in the frame
  advances by this value.
- The layout of `TimeInfo`, 0x18 bytes:

| Offset | Field |
|---|---|
| +0x00 | `freezeDepth`: a nesting counter, not a flag (read 2026-10-27: ADD CL,1 at 0x0047D127, SUB AL,1 at 0x0047D0F9); nonzero: the frame's seconds are written as 0 |
| +0x04 | `fixedStepMs`: the fixed step in ms or 0 for variable mode; that iFPSClamp writes it is unverified (no writer found yet) |
| +0x08 | `fixedStepCarryMs`: the fixed step's carried fraction |
| +0x0C | `frameSeconds`: the frame's seconds |
| +0x10 | `clockMs`: in variable mode tick - startTick at the last update; in fixed mode a counter advanced by the step |
| +0x14 | `startTick`: `GetTickCount` at construction, shifted by Freeze/Unfreeze |

## What that costs (measured, derived)

The clock is coarse in two ways.

- **The tick's own steps** (unverified here). `GetTickCount` moves with the
  system clock interrupt, which is 10 to 16 ms on Windows unless something
  raised the timer resolution. An 11 ms frame then reads as 0 or 16. This is
  the "64 Hz bug" that Oblivion Stutter Remover's `bFix64Hertz` and Oblivion
  Display Tweaks' `iTickFix` exist for. Bare `GetTickCount` was not measured
  in OBVR's runs, because the test games carry Display Tweaks.
- **Whole milliseconds, even with an exact tick** (measured). Under Proton
  at 75 Hz, with Display Tweaks' QPC-based tick in place and OBVR's
  `[Debug] FrameClockTrace=1` on, a 13.33 ms period was read as 13.0 ms on
  460 of 750 frames and as 14.0 ms on 230. The rest were dropped frames.
  So Display Tweaks' tick fix does not remove the quantisation (derived).
- **The effect** (derived). Every animation and every moving thing runs
  about 3.7 % fast or slow, alternating from frame to frame. None of a
  headset's periods (75, 90, 120 Hz) is a whole number of milliseconds, and
  neither is 144 Hz on a monitor.

## The fix OBVR built (read in OBVR's code, measured)

`[Render] PreciseFrameClock=1`, on by default: an entry detour on 0x0047D170.

- The engine's update runs as written, so its integer bookkeeping at +0x10
  stays the engine's.
- Afterwards the detour replaces the seconds at +0x0C with the performance
  counter's delta. It holds the delta to the engine's own range (1 to
  166 ms) and multiplies it by 0x00B06704.
- It leaves the frame alone when the clock is frozen, when iFPSClamp's
  fixed step is active, and on the first measured frame.
- The detour patches the update's first nine bytes:
  `mov edx,[esp+4]; fldz; sub edx,[ecx+14h]`. These are whole instructions
  with nothing relative in them.
- Measured with it: the given length reads 13.333 ms mean at 75 Hz (OBVR
  run 2026-10-23 14:59). It ran beside Display Tweaks' tick fix without a
  conflict.
- The same detour is exported as the standalone plugin "Oblivion Frame
  Fixes" (local only). In the game beside OBVR it showed 0 mismatches.

## What a modern engine does instead

- Measure the frame with a high-resolution monotonic counter, in floating
  point seconds, not whole milliseconds. In the reference loops of Gaffer on
  Games, "Fix Your Timestep!", the frame time comes from
  `hires_time_in_seconds()` and is clamped
  (`if (frameTime > 0.25) frameTime = 0.25;`). Read in
  https://gafferongames.com/post/fix_your_timestep/.
- Decouple the simulation from the render rate with an accumulator: step
  the simulation in fixed `dt`, and render a state interpolated by
  `alpha = accumulator / dt` between the previous and the current step.
  Same source. Oblivion's own Havok steps at its own rate. OBVR measured
  the chase camera stepping with Havok at 60 Hz, not every frame.
- For OBNG (derived): feeding the frame's seconds from the counter is the
  smallest correct change. A fixed simulation step with interpolation is the
  larger one, and it touches every time-driven system.

## From the frame-clock slice (2026-10-26, run wf_b24db2af-f80)

Seven functions mapped from the Ghidra export, each tried by three skeptics;
details per function in `re/functions/<address>.md`, states in
`re/ledger.md`. What the slice added to the above:

- **TimeInfo is a class with several instances, not a singleton** (read).
  The LoadingMenu constructor 0x005AD0D0 allocates one with
  `FormHeap_Allocate(0x18)` at 0x005AD2E7, constructs it through 0x0047D150
  (thiscall, the start tick as argument) and keeps it at LoadingMenu+0x54;
  LoadingMenu::Tick 0x005AD980 calls the update 0x0047D170 on that private
  instance at 0x005AD9C2. The global at 0x00B33E90 is the main loop's
  instance, updated at 0x0040D8AB inside the update step 0x0040D800.
- **Two globals shared by every instance** (read): the update writes this
  frame's tick to 0x00B39DB0 and the previous value to 0x00B39DB4 before the
  frozen test (0x0047D179..0x0047D186), whichever instance it runs on. Who
  reads them is open.
- **The update's exact paths** (read, confirmed by all skeptics): frozen
  (+0x00 nonzero) writes 0 seconds; fixed step (+0x04 nonzero) ignores the
  tick and advances +0x10 by the step with the carry at +0x08, the sum
  rounded to float every frame (FSTP at 0x0047D1A5) - so the advance is
  approximate, not "exactly the step"; variable mode takes tick - +0x10, a
  zero reads as 1, above the ceiling at 0x00A3D14C reads as 0xA6. The
  "first unfrozen frame reads as 166 ms" only holds in variable mode and
  only when the frozen stretch plus the frame exceeds the ceiling
  (derived). The three constants 0x00A2FC78, 0x00A30E40, 0x00A3D14C were
  not in the export; their values (2^32, 0.001, 166.67) are unverified
  here.
- **The clock is frozen by a bracket pair** (read): TimeInfo::Freeze
  0x0047D120 / TimeInfo::Unfreeze 0x0047D0F0 are called on g_timeInfo
  around work that must not count as frame time - by GameMessageBoxA
  0x004949E0 (`__stdcall(text, caption, type)`, ret 0xC) and by
  TESObjectREFR::LoadBaseFormModels 0x004E1580 while a reference's models
  load. The pair is a nesting counter at +0x00 (time-info slice,
  2026-10-27): Freeze increments it and, on the 0->1 transition only, reads
  GetTickCount once and rewrites +0x10 := now - +0x10 and +0x14 := now -
  +0x14; Unfreeze decrements it and, on 1->0, applies the same rewrite. So
  across one outermost bracket both fields come out shifted forward by the
  frozen stretch D, and the next update's delta is the real delta minus D
  (derived from the two bodies; whether an update ever runs inside a
  bracket, which would change this, is unverified). The earlier wording
  "the next unfrozen frame sees the whole frozen stretch, clamped" was the
  emulator run of Update alone, without the bracket; with the bracket the
  stretch is shifted out.
- **What the clock globals hold** (read, corrected 2026-10-27): 0x00B39DB0
  is tick - startTick of the instance last updated (its clockMs), not a raw
  tick, and 0x00B39DB4 the previous value. Their one reader found so far is
  BSFaceGenNiNode::UpdateDownwardPass 0x0055D4C0 (proposal), which hands
  the difference of the two, times (double)0.001f, to the FaceGen animation
  as its time step - without the ceiling and without fGlobalTimeMultiplier.
- **The physics step planner** (read, 2026-10-27, map only): 0x00889810,
  the update step's callee right after the clock update, calls nothing and
  schedules the Havok steps from the frame seconds plus a carry
  (0x00BA7910): step count 0x00BA7914, step length 0x00BA790C, by the
  update type 0x00BA7918 - 0 = fixed step of 1/60 s (0x00B2E2E8, OBVR: the
  runtime copy of fMaxTime:HAVOK, unverified here) with the carry clamped
  to one step, 10 = smoothed, else the frame split into equal steps; at
  most 3 steps, 2 when the flag 0x00B333B8 is set. It compares the frame
  seconds against the same 166.67 constant 0x00A3D14C the clock uses as
  milliseconds (why is unverified).
  **Measured** (2026-10-27, OBNG.dll's probe sampling 0x00BA7918 and
  0x00BA7914 every frame, Linux, 75 Hz, headless, OBVR loaded): update
  type 0 the whole run; in play one step on 99.5 % of the frames, two on
  the rest, never zero, and the step length 0x00BA790C read 0.013333 s.
  **Second mapping pass** (re/functions/0x00889810.md, loading-menu slice,
  held with all three skeptics): that 0.013333 is not the frame's length
  but OBVR's own STEP - OBVR's `StepPhysicsRate` (OBVR
  src/game/HandBodies.cpp:342-360) writes 1/rate = 1/75 into 0x00B2E2E8 at
  runtime, so the probe measured the planner under OBVR's setting. Under
  type 0 the global at 0x00B2E2E8 is a **maximum** step, not a fixed one
  (`g_havokMaxStepSeconds`): with T = frame seconds + carry, T >= STEP gives
  one step of STEP (P2) and the rest into the carry, STEP/2 <= T < STEP a
  half-step path (P4), smaller T no step; the carry is clamped to one
  step; a float32 model of the branch reproduces the run (P2 on 7498 of
  7500 frames, 2 on two) and, with STEP = 1/60 at 90 Hz, the alternation
  OBVR measured as "60 Hz physics, one frame in three without a step"
  (hand-weapon-collision-spec.md:636-646) - the two readings agree once
  OBVR's rewrite of STEP is known. A 2-step frame comes from a carry near
  STEP (a hitch frame drained over the following ones) or from a frame of
  two display periods. Not run through the emulator; the 90 Hz numbers are
  the model's (derived).
- **The time multiplier at 0x00B06704 is the INI setting
  `fGlobalTimeMultiplier` ([General])** (read, 2026-10-27): the static
  initialiser 0x009DC2D0 registers the SettingInfo (value +0x00 = 1.0f,
  name pointer +0x04 = 0x00A3D150, the string "fGlobalTimeMultiplier:General")
  with the IniSettingCollection at 0x00B07BF0 through 0x004040E0. The
  constants of the update, read from the image with the data export:
  0x00A3D14C = 166.66667f (the ceiling), 0x00A30E40 = 0.0010000000474974513
  (the double of the float 0.001f, so the seconds carry the float's
  rounding), 0x00A2FC78 = 4294967296.0f (2^32, the unsigned-to-float
  fix-up; 469 references in the image).
- **The update, executed** (measured in the emulator, 2026-10-27,
  `tools/difftest/emulate.py` on the exported bytes with the image's
  constants, `tools/difftest/cases/0x0047D170.json`): 13 ms at multiplier
  1.0 gives +0x0C = 0.013f and +0x10 = this tick; a zero delta gives 0.001f;
  500 ms gives 0.166f; frozen writes 0.0f and leaves +0x10 alone (so the
  next unfrozen frame sees the whole frozen stretch, clamped); the fixed
  step uses the float step at +0x04 for the seconds and carries the
  fraction at +0x08; multiplier 0.5 halves the seconds in a second write
  to +0x0C (the first write at 0x0047D241 is the unscaled value); the tick
  globals 0x00B39DB0/4 are written in every mode. The 24-bit x87 precision
  control gave the same result as 64-bit for 13 ms.
- **The game runs the x87 at 24-bit precision control, rounding to
  nearest** (measured 2026-10-27, OBNG.dll's probe at the first
  TimeInfo::Update under Proton: `_control87` 0x000A001F). So rebuilt
  float code must reproduce 24-bit x87 semantics, and the correctness
  review of LoadingMenu::Tick (results differ between 24- and 53-bit for
  timer 5.0, sinceLast -100) names the case that matters. Whether
  Windows runs the same word is unverified (D3D9 sets it the same way
  unless the device is created with FPU_PRESERVE; not checked).
- **Who writes the fixed step** (read 2026-10-27, clock-callers slice):
  two writers of g_timeInfo.fixedStepMs, nothing else. (1) The start-up
  sequence 0x00406360 (InitializeGame, proposal) reads the dword at
  0x00B02D10 and, when 1 <= v <= 199, stores 1000/v as float at 0x0040692B
  (FDIVR double 1000.0 at 0x00A2FC70); otherwise the builder's 0 stays and
  the clock runs in variable mode. That 0x00B02D10 is iFPSClamp (OBVR's
  reading) is unverified - its name pointer would settle it. (2) The
  render-and-present routine 0x0040D4D0 (OSGlobals::RenderFrame, proposal)
  writes it only inside the camera-path capture toggle (QueryControlState
  0x1F): 1000/N when the capture turns on with N != 0, 0.0f otherwise
  (0x0040D7A3 / 0x0040D7AD / 0x0040D7B7) - a debug recording feature. The
  carry +0x08 is written only by the builder 0x009DC2A0.
- **The clock stops while the window is inactive** (read 2026-10-27): the
  game window's WNDPROC 0x004060F0 (MainWindowProc, proposal) handles
  WM_ACTIVATE by calling TimeInfo::Unfreeze on g_timeInfo when the window
  becomes active and TimeInfo::Freeze when it goes inactive, the state in
  OSGlobals+0x02. The first activation at start is an Unfreeze without a
  Freeze (the counter-zero flow the shadow runs counted). Together with
  GameMessageBoxA's bracket around its modal loop, these are the three
  users of the bracket found so far (the third: LoadBaseFormModels).
- **The frame body** (read 2026-10-27): 0x0040D800 (OSGlobals::UpdateStep,
  OBVR's name kept) is the whole frame: GetTickCount once into
  TimeInfo::Update at 0x0040D8AB, six reads of frameSeconds (the first
  into PlanHavokSteps at 0x0040D8B0), the subsystems gated by fourteen
  IsMenuMode calls, and at its end the render-and-present routine
  0x0040D4D0 at 0x0040DF54. OBVR's "master frame call" 0x0040D6F7 is the
  interface pass 0x00579260 inside 0x0040D4D0. Who calls 0x0040D800 is still
  unread (candidate: the main loop near xOBSE's main-loop hook 0x0040F19D).
- **The global's builder is a CRT initialiser** (read 2026-10-27): the
  dword 0x009DC2A0 sits at 0x00A2884C in .rdata, directly followed by
  0x009DC2D0 (the fGlobalTimeMultiplier registration) at 0x00A28850 - two
  entries of the initialiser table, so g_timeInfo is built at startup
  before any plugin loads (which also means its builder can never be
  shadowed in the game; the emulator is its gate).
- **The TimeInfo class verified in the game** (measured 2026-10-27,
  tools/shadow-run.sh, artifacts/shadow/obng-shadow/20261027-082656):
  Update 108,574 calls, Init 4, Freeze 31, Unfreeze 32 - 0 mismatches
  between the original and the rebuild, on the main loop's instance and
  the LoadingMenu's. The LoadingMenu ticks its Update thousands of times a
  second while loading (67,735 calls in the first ten seconds, against ~750
  a window in play at 75 Hz). One Unfreeze more than Freeze: an unbalanced
  call took the counter-zero flow, as the mapping has it.
- **The update step 0x0040D800** (read): 524 instructions, 106 calls, 14
  calls to IsMenuMode 0x00578F60; 0x00889810 at 0x0040D8C2 is a direct
  callee in the Havok address range that receives the frame's seconds. No
  caller in the export; OBVR's master frame call 0x0040D6F7 is the
  candidate (unverified).
