# Which VR runtime API a rebuilt engine should speak, and what the choice unlocks

Status: knowledge, written 2026-11-03 from OBVR (`../OBVR`,
docs/vr-modding/architecture.md "Why OpenVR and not OpenXR",
docs/vr-modding/ecosystem-and-prior-art.md). Nothing of this is built in
OBNG. The trigger was a Discord question (333hronos, 2026-11-03) whether OBVR
could move to OpenXR to use the OFXR Bridge / VectorXR frame generation.

Marks: **read** = read in a binary or a header, **measured** = measured in
the game, **derived** = follows from the two, **unverified** = not checked.

## What Oblivion's shape forces today (read)

- Oblivion.exe is a 32-bit D3D9 process. OBVR is a 32-bit xOBSE plugin in
  that process; it speaks OpenVR (`openvr_api.dll` ships for x86) and hands
  the eyes to the compositor as Vulkan images it gets from DXVK's
  `ID3D9VkInteropTexture` (`src/render/EyeMirror.cpp`,
  `src/vr/OpenVRBackend.cpp`, texture type Vulkan). DXVK is a requirement.
- SteamVR's OpenXR runtime took 32-bit applications only since beta 2.17.2
  (2026-06-11), stable with 2.17 on 2026-09-10; beta 2.19.1 (2026-11-02)
  added a symlink for the 32-bit runtime "which can also be used by Proton"
  (Steam announcements for app 250820).
- The OpenXR loader keeps 32-bit and 64-bit runtimes and API layers in
  separate registry silos (`Wow6432Node`) and never crosses them
  (KhronosGroup/OpenXR-SDK-Source `src/loader/manifest_file.cpp`,
  CHANGELOG "Don't cross 32/64 registry silos"). A layer DLL must match the
  application's bitness.

## The frame-generation layers are x64 only (read 2026-11-03)

| Layer | Release read | Binaries | PE machine |
|---|---|---|---|
| OFXR Bridge (tig3rmast3r) | 0.3.2 v773, 2026-11-02, `OFXR-BRIDGE-0.3.2-v773.zip` | all 7 DLLs and the tray EXE | 0x8664 (x64) |
| VectorXR (DienerTech) | 0.20.0, 2026-10-30, `VectorXR-0.20.0-windows-x64-setup.exe` | `XR_APILAYER_DIENERTECH_VECTORXR.dll`, `vectorxr-app.exe` | 0x8664 (x64) |

Both are OpenXR API layers; OFXR's README: "Games built on OpenVR ... are
not supported ... the bridge never loads in them." Both do colour-only
optical flow by default (no game motion vectors needed); OFXR 0.3.x adds
DLSS-G and FSR 3.1 routes that need the game's own D3D12 DLSS motion vectors
and depth, VectorXR uses depth where the game sends it (D3D12, Vulkan).

## What follows for a rebuilt engine (derived)

- A 64-bit OpenXR-native renderer gets these layers, VDXR without SteamVR,
  and SteamVR's own path for free: no bridge, no second process. This is one
  more reason the renderer rewrite pays (beside single-pass stereo, see
  OBVR docs/performance-spec.md section 2).
- Motion vectors and depth should be first-class outputs of the renderer
  from day one: `XR_KHR_composition_layer_depth` and `XR_FB_space_warp`
  exist (OpenXR 1.1 registry), and every better frame-generation route
  above wants them. OBVR cannot provide them: the D3D9 engine writes no
  motion vectors.
- For OBVR itself the only road to those layers is a 64-bit host process
  that is the OpenXR application and receives the frames across processes
  (praydog/FEAR2VR's `xr64.exe` shared-memory publisher is the precedent).
  **Unverified** whether DXVK's 32-bit Vulkan images can be exported as
  Win32 external-memory handles for that; rejected in OBVR as oversized
  (docs/vr-modding/failed-approaches.md).

## Not measured

No frame-rate numbers for any of this; the "~15 % from OpenXR" figure in
the Discord question has no source and was not checked. DLSS stays out for
Oblivion regardless of the runtime API: NGX is x64 only and has no D3D9
path (OBVR memory of 2026-08-28, deepwiki NVIDIA/DLSS).
