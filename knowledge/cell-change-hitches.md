# Hitches on leaving a house and at exterior cell borders

Status: knowledge, written 2026-10-25 from OBVR (`../OBVR`,
docs/performance-spec.md section 6, harness runs
`artifacts/hand-script/perf-cell-border/20261025-140506` and
`perf-cell-walk/20261025-141402`). Nothing of this is built in OBNG.

Marks: **read** = read in a binary, **measured** = measured in the game,
**derived** = follows from the two, **unverified** = not checked.

## Setup of the measurement

- Linux, Proton, DXVK, RTX 4090, 75 Hz, OBVR's dual pass (the scene
  rendered once per eye), headless harness.
- Load order: the DLCs, Knights, OblivionWalkers, Ragdolls,
  My Voice Extender, Enhanced Grabbing, Ascension and Maskar's Oblivion
  Overhaul, plus about 20 OBSE plugins. OblivionWalkers' spawn
  replacement was off, but its frame tick and its ESP were in.
- Oblivion.ini: `bBackgroundCellLoads=1`, `uGridsToLoad=5`,
  `iPreloadSizeLimit=26214400` (read).
- OBVR's profiler recorded frame and event times; its render sampler took
  the main thread's eip and 8 stack words about every millisecond.

## What was measured

- **Leaving a house** (`player.moveto` from an interior to an exterior
  reference, which is a load like a door's):
  - The loading screen: 3.4 s of 100 to 300 ms frames.
  - Then the **first world frame takes 158.7 ms**.
  - 13 uneven frames follow (23 and 16 ms alternating, half of their time
    in SteamVR's vrclient.dll re-syncing). From 0.3 s on: 75 Hz clean.
- **Walking across an exterior cell border** (36 units a second):
  - Right at the first crossing: frames of 24 to 32 ms.
    - A third of their samples sit in ntdll under 0x00893B90, 0x008DFFB0
      and 0x008E4590, the Havok range, waiting on a lock (read from the
      stacks).
    - That this is the physics attach of the new cells is derived.
  - On the second leg into not-yet-visited cells: **one frame of
    107.8 ms**, about a second after the crossing (with background cell
    loads on).
  - Crossing the same border back and forth afterwards, with the cells
    still in memory: nothing over 32 ms.
- **The grass build** (0x004EBDA0): the first build after a load costs
  20.7 ms (`cow`) and 33.0 ms (the house exit). Each rebuild while walking
  costs 0.5 to 1.05 ms.

## Whose the long frames are

- **77 % of the 158.7 ms frame and 62 % of the 107.8 ms frame** are one
  loop in xOBSE (measured). The loop is `obse_1_2_416.dll` RVA 0x91480 to
  0x91495 (image base 0x10000000).
  - Read in the disassembly: it walks a linked list (node +0x0 data,
    +0x4 next) comparing `*data` with a 16-bit index the code just read
    from the script's byte stream.
  - deepwiki (llde/xOBSE) places this in `ScriptToken::ReadFrom`,
    obse/obse/ScriptTokens.cpp. A token that names another script's
    variable reads the variable index and looks it up with
    `ScriptEventList::GetVariable`, which walks the list.
- 50 of the capture's 58 samples in that loop fall in the single 107.8 ms
  frame. It is a burst, not a steady cost (measured).
- The engine function under it in 67 % of that frame is 0x005169A0, the
  executor of one script line (one command) (read). Corrected 2026-10-25:
  this file first said that xOBSE's `ScriptRunner::Run` hook address
  0x0051737F lies inside 0x005169A0. It does not. It lies in the next
  function, 0x00517330, the run of a whole script, which calls 0x005169A0
  at 0x0051754D (read in the disassembly; the function table has both
  starts).
- So a mod script that the engine runs when new cells or references come
  in evaluates OBSE expressions over another script's variables very many
  times (derived). Which script is **unknown**. Maskar's Oblivion
  Overhaul is the first suspect (unverified).
- OBVR's own spans in the 158.7 ms frame add up to about 18 ms. 12.5 ms of
  it is the scene pass for both eyes on a first draw, against 2.9 ms a
  pass in the steady state (measured).

## Whose script: the script clock (measured 2026-10-25 17:36)

OBVR's `[Debug] ScriptTimeTrace` times every script run through 0x00517330
(see below) and names the scripts by form ID. Harness run
`artifacts/hand-script/script-clock/20261025-173633`, same install:

- **The house exit.**
  - The first world frame took 160.4 ms in the profiler. 137.0 ms of it
    was scripts (measured); the clock's tick lies inside that frame.
  - 127.4 ms was one run of Maskar's Oblivion Overhaul's
    `MOOUpdateWorldSpacesAddNewFunctionScript`.
    - That it is MOO's: the form ID's first byte is MOO's place in the load
      order, derived from the plugins' file times. Its low 24 bits match
      MOO's SCPT record of that editor ID (read).
  - The script's source (read) builds an event map for a worldspace MOO
    has not seen yet: width x height x density / 100 iterations over
    arrays in MOO's quest script. That is where xOBSE's variable lookup
    runs.
  - The harness save predates MOO, so every run builds the map anew.
  - A second exit in the same session (run `20261025-181252`) had no MOO
    time (measured). Its 120.8 ms frame was the load itself: 105 ms in
    the harness's moveto, with the cells still in memory.
  - Whether a saved game keeps the map across a save and a load is
    unverified.
- **The border walk.** One frame had 148.4 ms of scripts in 5135 runs,
  all MOO's dynamic-dungeon ("outpost") generator functions (measured,
  names read).
- **Between the hitches**, all scripts together cost 0.10 to 0.16 ms a
  frame (measured).
- **Derived for an engine:** a mod that generates content when a place is
  first seen needs a budget across frames or a worker. The engine's
  script VM gives it neither, so one first visit costs one long frame.

## The variable lookup made an index (measured 2026-10-25 19:09-19:18)

Oblivion Frame Fixes' `FastVariableLookup` replaces xOBSE 22.13's inlined
list walk (obse_1_2_416.dll RVA 0x91479) with an index by id for long
lists. It forgets a list when the engine frees its variables (0x004FA080,
the only free found).

- **MOO's map build on the house exit:** 127.6 ms without, 30.4 ms with
  (measured, same scenario, same save).
- **The exit's longest frame:** 164.0 ms without, 75.7 ms with. Those
  75.7 ms are the first draw, not scripts (measured).
- **Correctness:** 800,001 lookups compared with the walk, 0 mismatches
  (measured).
- **So the lookup was about three quarters of MOO's cost** (derived).
  The rest is MOO's own work plus RTTI casts, about 11 % of the remaining
  frame's samples (measured). They are **not** the casts of xOBSE's
  `EventListFromForm`: a cache for exactly those (2026-10-26, 400,001 casts
  checked, 0 mismatches) changed MOO's frames by about a millisecond, inside
  the spread (measured). Who casts instead is unverified - xOBSE's command
  handlers cast their arguments at 523 sites.
- **The engine's own variable walk** (0x004FA110, a one-entry cache that
  xOBSE switches off, then a list walk): 14 of 57 samples of a 65 ms MOO
  outpost frame (measured once, 2026-10-25); the engine's command path
  calls it, xOBSE's index does not cover it.
- **Derived for an engine:** resolve a cross-script variable to a slot once
  when the script loads, and never cast or search per token. Then the 30 ms
  would shrink further, to the arithmetic MOO actually asks for.

## How the engine runs a script (read in Oblivion.exe 1.2.0.416, 2026-10-25)

- **The run of a script: 0x00517330.**
  - A thiscall on a ScriptRunner (0xA4 bytes) with eight arguments and
    `ret 20h`. In order:
    - the Script,
    - the reference it runs on,
    - the event list,
    - the containing reference,
    - a byte, two more bytes and a float, which only an effect script uses.
  - Its only callers, 0x005178FA and 0x0051793C, are in 0x00517810.
    0x00517810 takes the cached runner at 0x00B36208. While that runner is
    busy, it allocates a new one, so scripts nest: a script that makes
    another script run holds the inner run inside its own.
  - Script::Execute (0x004FBE00) and three calls at 0x004F9F20..0x004F9F82
    all reach 0x00517330 this way (read; the callers found by a call-target
    scan of the whole .text).
- **The Script record** (xOBSE Script.h, matching the disassembly):
  - +0x0C: the form ID.
  - +0x20: the data length.
  - +0x28: the type (0 object, 1 quest, 0x100 magic effect).
  - +0x30: the compiled data.
  - +0x34: a float, see the next item.
  - +0x38: the quest delay counter, which Script::Execute decrements by the
    frame's seconds.
- **A built-in script timer.**
  - When 0x005792E0 returns true, the run reads QueryPerformanceCounter
    before and after and adds the seconds to Script+0x34. 0x005792E0
    returns a byte at +0x50 of an object it gets from 0x00582160; that
    object is unidentified.
  - So the engine had per-script timing; it gives only a sum per script
    (read; what switches the byte on is unverified).
- **The line executor: 0x005169A0.** The run calls it once per command.
  xOBSE patches its prologue at 0x005169AE to keep the running Script for
  its error messages (xOBSE Hooks_Script.cpp, `kReturnCmdHook`).

## What a modern engine does instead (derived; not sourced from an engine)

- **Resolve script variables once.** A compiled script should turn a
  reference to another script's variable into a slot or pointer when it
  is compiled or loaded, not look it up by index through a linked list
  every time the token is read. At the least, the engine needs an indexed
  variable table (an array or a hash) per event list.
- **Budget script work on streaming events.** A cell-attach hook that runs
  heavy scripts should spread the work across frames, or run it off the
  main thread with the results applied later, and never inside the frame
  that first shows the new cells.
- **Measure per script.** A script profiler that shows the time per script
  and per block is what is missing here. OBVR's spec proposes one as an
  opt-in detour on the script runner.
