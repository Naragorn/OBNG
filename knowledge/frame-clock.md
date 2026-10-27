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
| +0x00 | frozen flag (nonzero: the frame's seconds are written as 0) |
| +0x04 | `1000 / iFPSClamp` or 0, the fixed step when iFPSClamp is set |
| +0x08 | the fixed step's carried fraction |
| +0x0C | the frame's seconds |
| +0x10 | the previous frame's milliseconds since the start |
| +0x14 | `GetTickCount` at the start |

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
- **The clock is frozen by a bracket pair** (read): 0x0047D120 / 0x0047D0F0
  are called on g_timeInfo around work that must not count as frame time -
  by GameMessageBoxA 0x004949E0 (`__stdcall(text, caption, type)`, ret 0xC)
  and by TESObjectREFR::LoadBaseFormModels 0x004E1580 while a reference's
  models load. Whether the pair is a flag or a nesting counter is open.
- **The time multiplier at 0x00B06704 is the INI setting
  `fGlobalTimeMultiplier` ([General])** (read, 2026-10-27): the static
  initialiser 0x009DC2D0 registers the SettingInfo (value +0x00 = 1.0f,
  name pointer +0x04 = 0x00A3D150, the string "fGlobalTimeMultiplier:General")
  with the IniSettingCollection at 0x00B07BF0 through 0x004040E0. The
  constants of the update, read from the image with the data export:
  0x00A3D14C = 166.66667f (the ceiling), 0x00A30E40 = 0.001 (double),
  0x00A2FC78 = 4294967296.0f (2^32, the unsigned-to-float fix-up; 469
  references in the image).
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
- **x87 precision matters for a rebuild** (derived, from the correctness
  review of LoadingMenu::Tick): the countdown arithmetic gives different
  results at 24-bit and 53-bit precision control for some inputs (timer
  5.0, sinceLast -100). Which control word the game runs under is
  unverified; it decides how rebuilt float code must be compiled or
  tested.
- **The update step 0x0040D800** (read): 524 instructions, 106 calls, 14
  calls to IsMenuMode 0x00578F60; 0x00889810 at 0x0040D8C2 is a direct
  callee in the Havok address range that receives the frame's seconds. No
  caller in the export; OBVR's master frame call 0x0040D6F7 is the
  candidate (unverified).
