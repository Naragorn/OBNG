# The render thread under a hardware profiler

What `perf` says about the render thread in a heavy indoor scene: how the cycles split between waiting for
instructions, waiting for data and real work, which module holds them inside the scene pass, and which single
instructions are slow. The instrument, what it found, and what was changed on it.

## The instrument (2026-11-24)

`perf` works on the game under Proton without root (`perf_event_paranoid` 2): `cycles:u`, AMD's IBS samples with
the software filter (`ibs_op/swfilt=1/u`, `ibs_fetch/swfilt=1/u`; plain `ibs_op//u` and `cycles:upp` are refused)
and the core's counters, the pipeline metrics among them (`perf stat -M PipelineL1,PipelineL2`). The machine is a
Ryzen 7 7800X3D (Zen 4: 32 KiB first-level caches, 1 MiB second level a core, 96 MiB third level).

- `tools/perf-record-run.sh <folder>` waits for the game of a harness run and for a mark of its hand script, then
  records the process for 42 s: the three sample kinds at 5 kHz each, and the render thread's counters. The render
  thread is the process's main thread.
- The recording's clock is CLOCK_MONOTONIC_RAW (`-k raw`), which is Wine's QueryPerformanceCounter in 100 ns
  ticks: a sample's time is a tick of OBVR's capture, so `tools/re/perf-pass.py <run> <perf folder>` keeps the
  render thread's samples that lie inside a `scene_pass` row of the capture's cpu_events.csv. Modules come from
  the capture's samples-modules.csv (Wine does not map a plugin's file, perf sees anonymous memory there), then
  from the process's maps; a module's functions from its link map (`--map`, `--module`, `--function`).
- `tools/measure-fight.sh <name> [keys]` is one such run of the tutorial's fight held still as Nadi plays it
  (`PERF=1` records it, `OBNG_DLL=<file>` runs another build without a deploy); `tools/machine-busy.py` is its
  test for a free machine.
- What an IBS operation sample is good for: its address is exact (cycles:u points at the instruction after the
  slow one), and it carries whether the operation is a load or store, whether it missed the data cache or the
  TLB and for how long, whether a branch was mispredicted, whether the instruction is microcoded, and two
  counts of cycles - from the operation's tag to its completion (its own delay) and from completion to retire
  (waiting behind older operations). A stalled instruction does not get more operation samples: where
  retirement waits is read from cycles:u, why from the operation samples around it.
- Two runs of the fight are not the same load (1 824 against 2 036 second-eye draws a frame in the two runs
  below). Compared per draw they agree: D3D9.DLL took 1.174 and 1.173 us a second-eye draw. So a module's time
  is compared per draw, with D3D9.DLL as the control.

## The tutorial's fight, the routes as played (measured 2026-11-24)

Run obng-profile-prison-fight-hold/20261124-151755 (OBNG.dll 7a21897b, OBVR.dll md5 d5dad000, Linux, the stand-in
headset at 72 Hz), 42 s from the mark h01, 3 012 scene passes of 7.00 ms.

**The render thread, the whole frame** (user mode, of its cycles): 38.8 % retiring, 22.6 % waiting for the front
end (14.0 % latency - instructions not there - and 8.6 % bandwidth), 32.2 % waiting for the back end (25.6 %
memory, 6.6 % CPU), 4.7 % bad speculation. 2.12 instructions a cycle. A frame is 81 million instructions and 38
million cycles, with 2.3 million first-level instruction cache misses, 342 000 second-level misses (123 000 of
them code), 126 000 mispredicted branches, 26 500 data TLB misses and 1.09 million loads that an older store
blocked (`ls_bad_status2.stli_other`).

**Inside the scene pass, by module** (ms a frame by the cycles): D3D9.DLL (DXVK) 2.39, Oblivion.exe 2.08, OBNG.dll
1.29, OBVR.dll 0.34, d3dx9_27.dll 0.21, ntdll.dll 0.20, FoliageAA.dll 0.14, kernel32.dll 0.14,
OblivionFrameFixes.dll 0.09, msvcrt.dll 0.08. DXVK is the largest, before the game's own code (OBVR's sampler,
at 1 kHz with a thread suspended, had them the other way round).

**Instruction fetch** (IBS fetch samples; of 1 000 fetches those that miss the first-level cache, and the mean
fetch latency in cycles): OBNG.dll 228 and 4.9, Oblivion.exe 153 and 2.6, D3D9.DLL 107 and 2.4, OBVR.dll 99 and
1.9, FoliageAA.dll 41 and 0.7. OBNG.dll has 18 % of the pass's cycles and 27 % of its fetch latency: its per-draw
code is visited between long stays in other modules and is cold each time. Second-level misses of code are rare
in the samples (1 - 2 of 1 000); the instruction TLB's first level misses for 40 - 50 of 1 000.

**Data** is not what the pass waits for: 50 - 80 of 1 000 loads miss the first-level cache, 15 - 20 the second,
and the miss latency is 0.4 - 0.8 cycles a tagged operation against 17 - 26 cycles from tag to completion.

**The slow single instructions** (IBS, tag to completion, summed; share of all modules' in the pass):
- Oblivion.exe 0x0098131A, a microcoded string copy with cache misses, 91 cycles, 1.9 %;
- `rdtsc` in OBNG.dll, microcoded, 73 - 79 cycles each: draw instancing's `DecideRecord` (every record of a pass
  list, 1.8 %) and the occlusion culler's `DecideDraw` (every draw decision, 1.1 %) - the routes time themselves
  for their summary lines, in play too; the whole draw reads the clock once more (RouteStereoDraw.cpp);
- the walk up a geometry's parents for the camera followers (`RouteDrawPrimitive`, the branch at +0x370): 140
  cycles of dependent loads, a draw;
- locked operations in D3D9.DLL on lines that miss the second-level cache (73 - 105 cycles).

## Wide loads over narrow stores: three thin wrappers inlined (class A, measured 2026-11-24)

In OBNG.dll the instruction after three loads held 22 % of the module's cycles in the pass:
`RoutedDevice::DrawIndexedPrimitive` +0x24 (10.5 %: `movups xmm0, [the draw's arguments]`), `IssueDraws` +0x35
(6.4 %: `movsd xmm0, [the record's fields]`), `EnginePorts::Slot` +0x33 / +0x7a / +0xb6 (5.6 %: `movups xmm0,
[the shader call's arguments]`). Each was a small function of its own that read, four or two fields at a time,
what its caller had just written field by field: a wide load over narrow stores, which the processor cannot
forward from the store buffer and which waits for the stores to reach the cache. With the three always inlined
(`[[clang::always_inline]]`: `IssueDraws`, `RoutedDevice::DrawIndexedPrimitive`, both `Slot`s) the values stay in
registers; same calls, same values.

| run | OBNG.dll | pass ms | second-eye draws a frame | OBNG.dll ms | us a draw | D3D9.DLL us a draw |
|---|---|---:|---:|---:|---:|---:|
| 20261124-151755 | 7a21897b | 7.00 | 2 036 | 1.29 | 0.634 | 1.174 |
| 20261124-152414 | d2ac0027 | 6.25 | 1 824 | 0.99 | 0.543 | 1.173 |

**14 % less of OBNG.dll's time a draw, 0.19 ms a frame at the first run's load** (not the 0.29 ms the three
places held: part of the wait was not theirs). The blocked loads fell from 13.4 to 11.6 a thousand instructions
over the thread. After it OBNG.dll's 0.99 ms are the eye switch 0.27, `RouteDrawPrimitive` (the whole draw, now
with its parts) 0.24, the culler's `CullDrawRenderPass` 0.07, `DecideDraw` 0.07, instancing's `DecideRecord` 0.05
and `RouteIndexedDraw` 0.04.

What stays odd, and is not explained: the first SSE instruction with a memory operand after a call into the
device returns still holds the cycles in the eye switch (+0x3a, +0xbf) and in the draw loop (+0xba2, +0xe02).
By the operation samples those loads miss neither cache nor TLB and complete in 7 - 25 cycles, but wait 130 - 200
cycles to retire: something older, in the device call's tail, is not done. No module of the game uses AVX on
that path (DXVK and Wine's DLLs have no VEX instruction at all), so it is not the SSE / AVX state change.
