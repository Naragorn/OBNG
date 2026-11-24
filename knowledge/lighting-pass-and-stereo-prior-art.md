# One lighting pass a geometry, one draw for both eyes: who has built either

The two structural candidates of docs/performance-backlog.md ("2026-11-24: the tutorial's fight"): A, one lighting
pass a geometry instead of three to four pass-list records; B, one device draw for both eyes instead of two. What
Oblivion's graphics mods do about them, and what the engine's successors do. Findings of the rebuild itself go
here too, once it starts.

**The game itself has A** (found the same day, after this survey): Lighting30Shader lights a geometry in one pass
and is used for hair, heads, blended and morphing geometry only - knowledge/lighting-passes.md. None of the mods
below uses it for more.

## Oblivion's graphics mods do neither (read 2026-11-24)

The addresses looked for: the pass builder 0x0085AE20, the per-light passes 0x00859880, the fog pass 0x00854190,
GetRenderPasses 0x007D85D0, the lighting pass mask 0x00B42F40, the accumulator's walk over the pass lists
(0x007AE6F5).

- **Oblivion Reloaded** (github.com/llde/TESReloaded10, commit c98401c of 2026-11-10; source read). Replaces engine
  shader programs one for one (Extra/Shaders/OblivionReloaded/Shaders: 77 files named after the engine's programs -
  SLS2001 ... SLS2068, WATER, GRASS, HDR, NIGHTEYE; no SLS file has a loop over lights), adds its own shadow maps
  and screen effects. Of the addresses above only 0x007AE6F5 is in its code (Framework/Oblivion/Base.h:92,
  `RemoveFogPass`: the walk steps over list 0x190 and a screen effect draws the fog - see
  culling-and-draw-setup.md). No stereo code: the word is in NVIDIA's header files only; `SetStreamSourceFreq` is
  passed through by its device wrapper (Core/Device/Device.cpp:427) and never called by it.
- **Oblivion Reloaded E3** (github.com/mcstfuerson/TES-Reloaded, commit 76069c7 of 2024-07-07; source read). None
  of the addresses; the engine addresses it names in 0x007A0000 - 0x0085FFFF are among Oblivion Reloaded's but for
  one, the shader definition lookup 0x007B4290 that it calls. No stereo code.
- **Oblivion Reloaded Combined** (nexusmods.com/oblivion/mods/51927; no public source). Binary read: ORC.dll of
  version 1.9.3 (473 600 bytes, the Nexus archive ORC193-51927-1-9-3), scanned for the addresses as 32-bit
  constants at every byte offset. Found: 0x007AE6F5 (three times, with its return 0x007AE6FB - Oblivion Reloaded's
  fog hook) and the two shader object sizes 0x00801BCB / 0x008023A1. Not found: the pass builder, the per-light
  passes, the fog pass, GetRenderPasses, the pass mask. Its shader folder holds programs named after the engine's
  (ExtraShaders: 48 SLS, 20 PAR, 24 SM, 6 STB, 6 STLEAF). No string with "stereo" or "instanc"; its culling is
  named in strings ("Culling Objects", "Culling far LOD"). Limits: an address built at run time would be missed,
  and the current version 3.1.5 was not read (its changelog names culling, shadows, memory and "most calculations
  moved from render pass", nothing about fewer passes or draws - unverified beyond that).

**None of the three merges a geometry's lighting passes, and none draws two eyes with one draw** - they are
flat-screen mods. They keep the engine's pass lists and add draws (shadow maps) and screen passes. The nearest
thing to A is the fog pass taken out of the lists and drawn as one screen effect. None of them is in Nadi's game
(the plugin folders of both games, 2026-11-24).

What is of use from them: TESReloaded10's HLSL for the engine's programs is under GPL 3 with added terms (License.md:
unofficial binaries must be named so, no sale) - a reading aid for the pass builder's shader pairs; the programs
in the game's own shader package stay the proof. E3's source is "all rights reserved": reading only.

## The engine's successors do both

- **A in Skyrim SE: Community Shaders' Light Limit Fix** (github.com/doodlum/skyrim-community-shaders, read through
  DeepWiki: src/Features/LightLimitFix.cpp, package/Shaders/Lighting.hlsl). Clustered forward: the lights go into
  a buffer, compute shaders sort them into clusters, and the lighting pixel shader loops over its cluster's lights
  inside the geometry's one draw. That is Direct3D 11 (compute shaders, structured buffers). Derived for Direct3D
  9 and shader model 3: no compute shader and no buffer to index, but the engine already gives every geometry its
  own short light list, so the lights of one geometry as an array of pixel shader constants is the form that fits.
- **B in Skyrim VR, by Bethesda** (read: package/Shaders/DistantTree.hlsl and Common/VR.hlsli in the same
  repository, DeepWiki pages 8 and 8.1). One instanced draw for both eyes: the eye index is the instance id
  (`SV_InstanceID`), the per-eye matrices are arrays of two, both eyes share one side-by-side target, and a clip
  and a cull distance per eye keep each instance in its half. Derived for Direct3D 9: instancing exists
  (`SetStreamSourceFreq`), but shader model 3 has no instance id and no clip distance output - the eye index must
  come from an instance stream, and the cut between the halves from a pixel kill or from the backend. That is why
  B needs the vertex programs or DXVK changed.

So both are proven designs on this engine's line; what is open is their Direct3D 9 form, not the idea.
