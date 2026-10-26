# What SteamVR's compositor tells a VR renderer about its frame, and how it differs by headset

Status: knowledge, written 2026-10-26 from OBVR (`../OBVR`,
docs/performance-spec.md section 7; the captures
`artifacts/nadi-win-live/20261025-211700` and
`artifacts/hand-script/script-clock/20261025-200103`). Nothing of this is
built in OBNG.

Marks: **read** = read in a binary or a header, **measured** = measured in
the game, **derived** = follows from the two, **unverified** = not checked.

## The compositor's frame timing (read: openvr.h Compositor_FrameTiming)

- `WaitGetPoses` returns when the compositor hands out the poses for the
  next frame. How long before that frame's vsync it does so is reported
  afterwards for every finished frame as `m_flNewPosesReadyMs` (negative =
  before the vsync).
- `m_flPreSubmitGpuMs` spans the application's GPU work between
  WaitGetPoses and the second Submit on the GPU's timeline; it is not GPU
  busy time, gaps count (read from the field's comment; the Windows numbers
  below bear it out).
- `m_nNumFramePresents` 2 means the frame was shown twice: the compositor
  ran the application at half rate (interleaved reprojection on Linux,
  motion smoothing on Windows). `m_nReprojectionFlags` bit 0x100 (the
  throttle count) says the compositor held the application back.

## The running start differs by headset (measured)

| Headset, system | `newPosesReadyMs` on every frame | period |
|---|---|---|
| Bigscreen Beyond, 75 Hz, Linux SteamVR | -2.99 ms (7226 of 7342 frames) | 13.33 ms |
| Pimax Dream Air, 90 Hz, Windows SteamVR | -10.97 ms (5234 of 5234 frames) | 11.11 ms |

- The Beyond's compositor wakes the application about 3 ms before the vsync,
  Valve's "running start". The Pimax's wakes it right after the previous
  vsync, a whole period before the next one (measured). Why the Pimax's
  does so (its own runtime behind SteamVR, a setting) is unverified.
- Derived: a renderer that paces itself on "the poses come 3 ms before the
  vsync" is wrong by a period on the Pimax. OBVR held every frame until the
  vsync it had computed from that number - 9.3 ms a frame - and its frames
  grew from 4 ms of work to 13 ms; the compositor then throttled the game to
  half rate for 90 % of a 37-minute session (measured). The fix was to trust
  only a running start well below the period and to hold at most 4 ms.

## What a renderer should do (derived)

- Read the headset's refresh (`Prop_DisplayFrequency_Float`) and judge every
  compositor timing number against the period, never against a constant.
- Treat `newPosesReadyMs` near the period as "the compositor's frame starts
  at the vsync": the application has the whole period, and submitting early
  is right.
- Measure GPU cost with the application's own timestamp queries where they
  work (DXVK's did; the native D3D9 driver's returned 1 µs for a whole
  scene pass - measured, cause unverified), and take the compositor's
  pre-submit number as wall time between two markers, not as GPU load.
