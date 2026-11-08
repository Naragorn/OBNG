# The floating-point contract of a rebuilt function

What a rebuild of an x87 function must reproduce, and how to do it at the
engine's speed. Findings of 2026-10-30 from the first subsystem replacement
(the shadow light's range walk, `docs/subsystems/shadow-light-range-walk.md`).
Marks as in AGENTS.md: read / measured / derived / unverified.

## What the engine's x87 does (measured, read)

- Precision control 24 bits for the whole process (measured, FNSTCW in the
  loading menu and at the shadow walk's entry; `frame-clock.md`). FMUL,
  FADDP, FSUB round the significand to 24 bits in the register but keep the
  register's 15-bit exponent; FLD and FCOM are untouched; FSTP float rounds
  to the float format (SDM vol. 1 8.1.5.2, 4.8.4).
- The rounding control is NOT one value per process: the shadow pass calls
  the range walk with FCW 0x0C7F, truncate (measured 2026-10-30 13:18 by
  FXSAVE at 0x007D59E0's entry, artifacts/shadow/obng-range-walk-routed/
  20261030-131826), the loading menu runs at 0x007F, nearest. So the mode is
  an input of every rebuilt float function, read at the entry (FNSTCW), never
  a default. Who sets truncate for the shadow pass is open.
- MXCSR at the walk's entry was 0x00001FBF: nearest, every exception masked,
  every sticky flag already set, FTZ and DAZ off (measured, same run).

## x87 at precision 24 against SSE single (derived, measured on the host)

One x87 operation at precision control 24 and one SSE single-precision
operation round the exact result to the same 24-bit significand under the
same rounding control. They differ only in the exponent range: the x87
register keeps 15 exponent bits until the store, the single format 8. So the
two part only where an operation's result leaves the single's normal range -
overflow (OE), a tiny inexact result (UE), a denormal operand (DE; a tiny
exact intermediate used again) - or where the operation is invalid (IE:
inf - inf, 0 * inf). SSE reports exactly these in MXCSR's sticky flags while
its exceptions are masked (SDM vol. 1 11.5.2). ZE cannot occur without a
division; PE (inexact) is the ordinary case. A quiet NaN operand raises
nothing in either and propagates (payloads are not modelled; the comparisons
are false for any NaN).

Measured on the host's x87 running 0x0047DA70's instruction sequence
(tests/NiBound_WhichSideNativeTest.cpp, 4 x 200 000 edge-biased inputs under
the four control words): the native result with the fallback equals the x87
bit for bit on every input, and every raw native result that differed from
the x87's had one of the four flags raised (0 unreported of 2 492 / 15 512 /
15 828 / 38 081 differing raw results per mode). On game-like magnitudes
(2^-2 to 2^14) the native result was kept on 50 000 of 50 000 inputs.

## The shape every hot float rebuild takes (decided 2026-10-30)

1. Native arithmetic in the original's order, in single precision, with
   MXCSR set to the call site's rounding control (from the FCW), every
   exception masked, FTZ and DAZ off, flags cleared - one MXCSR scope per
   batch, the caller's register put back after it (nothing the walk calls
   between batches runs under a changed register).
2. The flags read after the batch: none of IE, DE, OE, UE raised - the
   results are the x87's; any raised - the exact software path (TwoSum and
   bit rounding, `NiBound_WhichSideLogic.h`) recomputes the batch.
3. The compiler is told: clang `#pragma STDC FENV_ACCESS ON` lowers the
   operations to constrained intrinsics with dynamic rounding and strict
   exceptions (measured in the generated code of clang++ -m32 and clang-cl
   23.1.4: the STMXCSR / LDMXCSR pair stays around the MULSS / ADDSS /
   SUBSS; LLVM issue 8472, status 2026-10-22, lists x86 as supported); MSVC
   `#pragma fenv_access (on)` disables the code motion, folding and
   contraction that assume the default environment
   (learn.microsoft.com/cpp/preprocessor/fenv-access). The MSVC build is not
   run on Linux: unverified here, the Windows CI builds it.
4. The exact software path stays: as the oracle in the tests and the
   emulator gates, and as the fallback. It is never the hot path - on the
   host it costs 314 ns per six-lane batch against 13.4 ns native with the
   MXCSR scope (tools/bench/range-walk-batch-bench.cpp, clang++ -m32 -O2,
   2026-10-30; the register accesses cost less than the benchmark resolves).

Why: the routed range walk with the software rounding cost 0.46 ms a scene
pass more than the engine's own walk on the fixed scene (measured 2026-10-30
15:28; OBNG.dll held 14.1 % of the in-pass samples, the engine's walk 3.1 %),
and the samples sat in the rounding's bit arithmetic, not in the walk.

In the game (measured 2026-10-30 17:21-17:30, fixed scene OBVRHarnessWalkers):
the shadow compared 45 137 walks with the native pass, 0 mismatches, under
rounding control nearest; the routed run (truncate at every entry) kept
1.54 million plane batches per 10 s natively, 0 recomputed, and OBNG.dll
held 1.8 % of the in-pass samples where the engine's own walk held 2.3 % -
scene_pass 2.83 against 2.78 ms, inside the spread of two original runs
(2.82 / 2.91 ms). The native-with-fallback shape runs at the engine's cost.

## When the rebuilt function is the hardware's own instruction (2026-11-08)

The CRT square root _CIsqrt 0x00982C30 (re/functions/0x00982C30.md) is one
FSQRT with a dispatch around it, and the dispatch is what costs: the game's
control words (0x007F, 0x0C7F; PM set, not 0x027F) send every inexact root
through __87except. Its rebuild (plugin/src/FastSqrtLogic.h) therefore takes
none of the shape above: no SSE arithmetic, no MXCSR scope, no software
fallback, because there is no arithmetic to reproduce - the replacement runs
the same FSQRT under the same control word ((word & 0x0300) | 0x007F: PC
kept, nearest, all masked) and inherits the 24-bit rounding at PC 24 from the
hardware, exactly as the original does. What is rebuilt are the decisions
around the instruction, written as generated machine code so the tests run
the same bytes the game runs. The oracle is the original's own bytes on the
host's x87 (plugin/tests/FastSqrtOracleTest.cpp), not the emulator: Unicorn's
FSQRT ignores precision control (measured; docs/re-workflow.md).
