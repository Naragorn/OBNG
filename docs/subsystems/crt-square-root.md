# The CRT square root's dispatch

What: a replacement for the dispatch around `_CIsqrt` 0x00982C30, the MSVC
helper every `sqrt()` in Oblivion.exe calls (145 call sites; NiBound::Merge,
BSFadeNode::OnVisible and the shadow light's range test among them). Why:
the game runs the x87 at precision control 24 with every exception masked
(control words 0x007F and 0x0C7F, measured in knowledge/frame-clock.md), and
the CRT's square root treats any word other than 0x027F as a reason to run
its exception-reporting path for every inexact result - __87except,
__handle_exc, __ctrlfp, the errno setter - which costs 1.7 % of the render
thread's out-of-pass samples and 1.2 % of the in-pass samples on the Gold
Road (knowledge/render-thread-outside-the-pass.md), about 0.2 ms a frame
(derived). The arithmetic itself is one FSQRT and stays one.

## The shape

- `plugin/src/FastSqrtLogic.h`: the decisions as plain functions over the
  stored double's high dword and the control word (`ImageIsSpecialOrNegative`,
  `ControlWordIsFast`, `SqrtControlWord`, `ControlWordNeedsSwap`, `Decide`),
  and `BuildFastSqrt`, which writes the 133 bytes of machine code that make
  those decisions at run time (CodeWriter, two passes for the two labels).
  The code: SUB ESP,12; FST qword [ESP]; sign or exponent 0x7FF in the high
  dword -> the original; FNSTCW; an exception unmasked or PC 64 / reserved
  -> the original; the caller's word already the square root's (0x007F,
  0x027F) -> FSQRT; else FLDCW (word & 0x0300) | 0x007F, FSQRT, FLDCW the
  caller's word. "The original" is `ADD ESP,12; JMP dword ptr [cell]`, the
  cell holding the detour's trampoline.
- `plugin/src/FastSqrt.cpp`: `[Route] Sqrt=1` puts the generated code on the
  entry (one detour); `[Shadow] Sqrt=1` puts a thunk there that FXSAVEs the
  x87 state, runs the original and the dispatch each from that state,
  compares the 80-bit result, the control word and the exception flags,
  records the call (summary name `_CIsqrt`), FXRSTORs and continues in the
  original. Both patch the same entry; the shadow wins when both are asked.
- The dispatch clobbers EAX only. The original clobbers EAX, ECX and EDX
  (measured; re/functions/0x00982C30.md).

## Gates

1. `plugin/tests/FastSqrtLogicTest.cpp`: every flow of the decisions, the
   bytes against a hand-assembled listing, and the generated code run on the
   host's x87 with a fake original behind the cell (200 000 random inputs
   over six fast words: the decided path, the root against a plain FSQRT
   under the square root's word, the control word back, the flags, EDX, the
   stack).
2. `plugin/tests/FastSqrtOracleTest.cpp`: the original's own bytes, mapped
   from the reference Oblivion.exe (.text 0x00980000.., .rdata, .data at
   their image addresses; __set_errno_from_matherr stubbed to record the
   type, since errno needs the CRT's thread data) and run on the host's x87
   beside the dispatch: 1.8 million calls over 0x007F, 0x0C7F, 0x047F,
   0x087F, 0x027F, 0x037F, 0 differences in result, control word, exception
   flags, stack top and errno writes (2026-11-08). The condition codes
   differ on about 29 % of PC-24 calls (the original's FLDPI/FSTP after the
   root); no caller reads them. Skipped (77) without the executable.
   The emulator layer is not used: Unicorn's FSQRT ignores precision
   control (docs/re-workflow.md).
3. In the game: `tools/shadow-run.sh --ini tools/hand-scripts/obng-sqrt-shadow.ini
   tools/hand-scripts/obng-profile-gold-road-single.txt --save OBVRHarnessMOO
   --run-timeout 480` - the Gold Road scenario with the shadow comparing
   every call; PASS is a summary with `_CIsqrt N calls 0 mismatches`.
4. The measurement: the same scenario with
   `tools/hand-scripts/obng-measure-profile-sqrt.ini` ([Route] Sqrt=1)
   against `obng-measure-profile.ini`, read with OBVR's perf-phases.py and
   perf-functions.py: the frame time and the samples under 0x00991BAB,
   0x00991AB7 and __87except.

## State

Verified 2026-11-08: gates 1 and 2 green; gate 3 ran at 17:48 (run
artifacts/shadow/obng-profile-gold-road-single/20261108-174827): 65 443 293
calls compared, 0 mismatches; gate 4 ran at 18:00 (run -180031 against the
baseline -175231): the CRT error path's samples went from 181 to 0 a window
out of the pass and its functions vanished from the in-pass CRT too, about
0.3 ms a frame (derived); the Linux harness's frame time is vsync-bound at
75 Hz and cannot show the gain. Details in
knowledge/render-thread-outside-the-pass.md, "Runs of 2026-11-08". On by
default in the shipped OBNG.ini since 2026-11-08 (Nadi's go).
