# How the game is quit, and how a VR runtime's quit reaches it

Status: knowledge, written 2026-11-01 from OBVR (`../OBVR`,
`src/vr/VrEvents.h`, `src/game/GameAddresses.h` kOsGlobalsPointer, harness
`tools/hand-scripts/steamvr-quit.txt`). Nothing of this is built in OBNG.

Marks: **read** = read in a binary or a header, **measured** = measured in
the game, **derived** = follows from the two, **unverified** = not checked.

## One byte decides (read)

- OSGlobals is the object behind the pointer at 0x00B33398. Its first byte
  is `quitGame`, its second `exitToMainMenu` (xOBSE `obse/GameOSDepend.h`,
  `class OSGlobals`: "UInt8 quitGame; // 00", "UInt8 exitToMainMenu; // 01").
- All three ways out of the game write that byte and nothing else at the
  point of decision - the same two instructions each time, read in the
  bytes of Oblivion.exe 1.2.416 (Steam), 2026-11-01:

  | Address | Bytes | Way out |
  |---|---|---|
  | 0x005077EA | `A1 98 33 B3 00` / `C6 00 01` | console `qqq` |
  | 0x005B5A0D | `A1 98 33 B3 00` / `C6 00 01` | main menu, Exit |
  | 0x005BDE60 | `8B 15 98 33 B3 00` / `C6 02 01` | pause menu's message box, Quit |

  xOBSE hooks its ExitGame / ExitGame_Console messages at exactly these
  three places (`obse/Hooks_Gameplay.cpp`, kExitGameFromIngameMenuPatchAddr
  and its two siblings).
- The pause menu's message box callback is 0x005BDDE0: button 2 sets
  `exitToMainMenu` (byte +0x01), button 3 hides two interface nodes, calls
  0x0057F170(0) and sets `quitGame`. The button comes from
  InterfaceManager+0xB0 (0x00578D70 reads it and resets it to 0xFF).
- Readers of the byte (decompiled export): 0x0059E580, 0x006136E0,
  0x0066B150 skip work once it is set. The main loop that leaves on it sits
  around xOBSE's main-loop hook 0x0040F19D and is not in the decompiled
  export (unverified which instruction reads it).

## Measured in the game (2026-11-01, OBVR harness, Windows, Steam EXE)

- Setting `quitGame = 1` from a plugin in the frame (OBVR's answer to
  SteamVR's VREvent_Quit) ends the process by itself within seconds, exit
  code 0 - the same as the console's `qqq` in a control run (exit code 0,
  the same log ending). Runs `artifacts/hand-script/steamvr-quit/20261101-095354`
  and `qqq-control/20261101-095501` in OBVR.
- Neither exit wrote a line OBVR logs from its DLL_PROCESS_DETACH: the
  game's own exit does not seem to reach the plugin's detach, or not with
  the log still writable (unverified which; no crash log, no Windows error
  event, exit code 0).

## What a VR runtime asks (read: openvr.h)

- SteamVR sends `VREvent_Quit` (700) to a scene application it wants gone
  (SteamVR closing, the dashboard's quit). The application answers with
  `IVRSystem::AcknowledgeQuit_Exiting` - "This extends the timeout until the
  process is killed" - and exits. Without the answer vrserver kills the
  process when its timeout runs out: unsaved progress is lost.
- `AcknowledgeQuit_Exiting` is entry 47 of VR_IVRSystem_FnTable
  (IVRSystem_026, openvr_capi.h, counted from GetRecommendedRenderTargetSize
  as 0).

## What a modern engine does instead

- One explicit "request quit" path that every source goes through - the
  menu, the console, the OS window's close, the VR runtime's quit - with the
  save-game prompt and the shutdown order in one place, rather than a byte
  that each caller writes after doing its own share of the work (the pause
  menu hides nodes first, the console does not).
- OpenXR has the same contract as a session state (XR_SESSION_STATE_EXITING
  after xrRequestExitSession or a runtime-initiated exit), which a rebuild
  would route into that one path (unverified against the OpenXR spec text;
  not read for this note).
