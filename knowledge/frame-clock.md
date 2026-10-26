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
