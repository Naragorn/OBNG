# A lit geometry's lighting: several passes by ShadowLightShader, one by the game's own Lighting30Shader

How the engine lights a geometry, by its own names: the chain of pass-list records ShadowLightShader builds, and the
one-pass shader the same EXE carries and uses for a few kinds of geometry only. Oblivion.exe 1.2.0.416; names are
the binary's own strings where it has them, proposals otherwise.

## The pass ids have names (read 2026-11-24)

0x007B4920 is a switch over the pass id (0 .. 0x1A2) whose every case returns a string; `tools/re/pass-names.py
<Oblivion.exe> [id ...]` reads its jump table (0x007B69D8) and the strings. TES Reloaded names the function
GetPassDescription. The ids of the tutorial's fight (knowledge/culling-and-draw-setup.md has their counts):

| id | name | what it is (derived from the name and the builder 0x0085AE20) |
|---|---|---|
| 0x60, 0x65 | BSSM_AD3, _S | base: ambient and three lights' diffuse, no texture; _S skinned |
| 0x76, 0x92 | BSSM_ADT, BSSM_ADT2 | base with the texture in it, one light / two lights |
| 0x84, 0xB9 | BSSM_ADT_Shp, BSSM_ADTS_Shp | the same with the projected (canopy) shadow; ADTS with specular |
| 0xC6 | BSSM_ADTS2 | base with texture and specular, two lights |
| 0xE8, 0xEB, 0xF6 | BSSM_DIFFUSEPT2, _S, BSSM_DIFFUSEPT3 | two / three further point lights, added |
| 0x104, 0x107, 0x108 | BSSM_TEXTURE, _S, _Vc | the texture multiplied over the summed light (_Vc: vertex colours) |
| 0x115 .. 0x122 | BSSM_2x_SPECULARDIR... | one light's specular, directional |
| 0x123, 0x126 | BSSM_2x_SPECULARPT, _S | one point light's specular, added; one record a light |
| 0x177 .. 0x17A | BSSM_2x_SIMPLESHADOW... | an actor's shadow received; one record a shadow light |
| 0x190, 0x192 | BSSM_FOG, _S | the fog, the list's last record |
| 0x12D .. 0x146 | BSSM_3XLIGHTING... | Lighting30Shader's one lighting pass (below) |
| 0x154, 0x155 | BSSM_3XDEPTHMAP, _S | Lighting30Shader's geometry in a shadow map |

So a geometry under three or more lights is AD3 + DIFFUSEPT2/3 + TEXTURE + a 2x_SPECULARPT a light + FOG, and one
under two is ADT2 + a 2x_SPECULARPT a light + FOG: the 1 900 records for about 430 lit geometries of the fight.

## The game's own one-pass lighting: Lighting30Shader (read 2026-11-24)

- **The classes.** Lighting30Shader (RTTI; vtable 0x00A930C4, constructor 0x007FAC40, made by 0x007FC7D0) is shader
  kind 0x1A of GetShaderDefinition 0x007B4290. Its property is Lighting30ShaderProperty (vtable 0x00A9576C, 0x108
  bytes, constructor 0x00863430), made for a geometry by the shader's slot +0x18 (0x007FF350).
- **The programs.** `SM3%03i.vso`, `SM3%03i.pso`, `SM3LL%03i.pso` (the EXE names their sources SM3Lighting.v.hlsl /
  .p.hlsl, SM3ZOnly, SM3DepthMap, SM3SimpleShadow). The shader package in use here (shaderpackage004.sdp,
  RendererInfo.txt: RenderPath BSSM_SV_2_A, PStarget ps_2_a) holds 91 of them, compiled for vs_2_0 and ps_2_x:
  `tools/re/sdp-shader.py <package> list|table|asm|save` lists a package and shows a program's constant table and
  instructions (vkd3d-compiler).
- **One program does it all.** SM3001.pso (335 instructions): the normal map, ambient (`AmbientColor` c0), the
  first light as a direction and up to seven more as points with `1 - (d / radius)^2` (`LightData` c9.., two
  registers a light; an unrolled loop that picks by `ToggleNumLights` c6), a specular term a light (`EyePosition`
  c1, nine `pow`), texture times vertex colour, and the fog as the last `lrp` (the vertex program computes the
  factor from `FogParam` / `FogColor`). SM3000.pso is the same without specular; the SM3LL programs hold four
  lights (`LightData` x8). Variants by sampler: `HairTint` / `LayerMap` / `AnisoMap` (hair), `FaceGenMap0/1`,
  `GlowMap`, `ShadowMap` + `ShadowMask` (the projected shadow, pass ids +0x0D "Shp").
- **The settings stay.** `ToggleADTS` c5 is the lighting pass mask 0x00B42F40 (bDoAmbientPass, bDoDiffusePass,
  bDoTexturePass, and bDoSpecularPass together with the property's specular flag) written as four 0 / 1 floats by
  the shader's per-record setup 0x007FF4A0; the light count is the record's, capped at 8 (0x00B2DCFC, set to 8 by
  the constructor on every render path below 7).
- **Its pass list** (GetRenderPasses 0x008637D0, slot +0x5C of the property): **one lighting record a geometry** -
  0x12D plain, 0x12E skinned, 0x12F hair, 0x130 parallax, 0x131 / 0x132 FaceGen, 0x133 / 0x134 glow, 0x135 .. 0x139
  with vertex colours, each +0x0D with the projected shadow - whose light array is the scene's first light and then
  every light of the geometry that is no shadow light (+0xF4 clear). Beside it only: a 3XZONLY record for a fading
  geometry, 3XTEXEFFECT, one 3XENVMAP record for an environment map, 3XDECAL records. No texture pass, no pass a
  light, no fog pass. A skinned geometry with parallax has no program ("SHADER ERROR : no shader to handle
  BSSM_3x_SPx").
- **What the list does not have: an actor's shadow.** The pass ids 0x14E .. 0x151 (3XLIGHTING_SIMPLESHADOW) exist
  and the shader's setup handles them (0x007FF4A0, 0x00862B40), but no builder makes a record with them: a
  geometry of this shader receives no actor shadow. ShadowLightShader's builder adds a 2x_SIMPLESHADOW record a
  shadow light (0x177 ..).

## Who gets it: the shader kind chooser 0x007D1920 (read in the instructions, 2026-11-24)

The object preparation 0x007B7FC0 gives a geometry prepared with kind 1 the kind `int __cdecl 0x007D1920(geometry,
a, b)` returns (its one call: 0x007B8091), then makes the property through that kind's shader (slot +0x18):

| returns | shader | for |
|---|---|---|
| 0x11 | WaterShader | a material named "lava" |
| 0x1A | Lighting30Shader | a material named "hair...", when the card's flag is set or the render path is 5 or more |
| 0x0E | SkinShader | a material named "skin" (render path 2 or more) |
| 0x0F | ParallaxShader | a texturing property whose apply mode bits are 8 |
| 0x1A | Lighting30Shader | a geometry that blends (alpha property bit 0) or whose material is named "dynalpha...", and one with a NiGeomMorpherController - when the card's flag is set |
| 1 | ShadowLightShader | everything else |

FaceGen heads get 0x1A by the same flag in 0x00553BB0 (0x0E without it). So today the one-pass shader lights hair,
heads, blended and morphing geometry - the cases several additive passes cannot draw right - and everything opaque
goes through the chain. Measured: the fight's logs name Lighting30Shader for pass ids 340 / 341 (its geometry in
the shadow maps; run obng-profile-prison-fight-hold/20261124-130042).

- **The card's flag** is the byte 0x00B42EA5: the render path's choice 0x007B45F0 sets it for a card whose vendor
  string begins "ati" or "nv" and that reports more than 255 pixel shader instruction slots; RendererInfo.txt's
  "3.0 Shaders: yes" is that byte (0x00498340). Set on this machine (maxPS20inst 512).
- **`bAllow30Shaders` does nothing**: no code reads the setting's value (0x00B06DAC is referenced by its
  constructor, its destructor and an unwind handler only).
- **Render path 7 (BSSM_SV_3_0) is never chosen**: 0x007B45F0 writes 0 .. 6 into 0x00B42F48, nothing writes 7.

## `[Probe] Lighting30Kinds` (built 2026-11-24, off)

`src/engine/render/Lighting30KindLogic.h`, tests/Lighting30KindLogicTest.cpp, plugin/src/Lighting30Kind.{h,cpp}. The
chooser's call at 0x007B8091 goes through the probe, which turns the game's answer 1 (bit 1 of the key), 0x0E (bit
2) or 0x0F (bit 4) into 0x1A when the card's flag is set, and counts what it moved. Every geometry prepared after
the plugin's start is concerned - all of them. It is a measuring instrument: moved geometry receives no actor
shadow (above), so the picture changes.

## Measured: the probe in the tutorial's fight and in the tutorial's first room (2026-11-24)

`[Probe] Lighting30Kinds=1` against 0, the same build for both (OBNG.dll md5 852878c0, swapped in per run), the
routes as played, `tools/measure-fight.sh`, Linux, the stand-in headset at 72 Hz (period 13.9 ms), the Dream Air's
frame size. The fight is tools/hand-scripts/obng-profile-prison-fight-hold.txt between the marks arrived and done
(`tools/re/phase-frames.py arrived done`); draws, records and the GPU's time are the means of the 17 summaries of
the hold (`[Probe] GpuPasses`: the GPU from the frame's first stamp to its last).

| run (obng-profile-prison-fight-hold/) | key | draws an eye pass | records a frame | scene pass ms (p90) | own work ms | GPU ms |
|---|---|---:|---:|---:|---:|---:|
| 20261124-214716 | 0 | 2 291 | 2 008 | 6.2 (7.0) | 8.2 | 6.24 |
| 20261124-214421 | 1 | 678 | 574 | 2.7 (3.1) | 4.5 | 4.03 |
| 20261124-220130 (filmed) | 0 | | | 6.2 (7.0) | 8.4 | |
| 20261124-215816 (filmed) | 1 | | | 2.7 (3.1) | 4.7 | |

**The scene pass falls from 6.2 to 2.7 ms (-56 %), the frame's own work from 8.2 to 4.5 ms (-45 %), the draws of an
eye pass from 2 291 to 678, the GPU's time from 6.2 to 4.0 ms.** Two runs a side agree to 0.1 ms. The tutorial's
first room held still (obng-prison-tutorial-alternate.txt, every frame's eye read back, so slower by itself;
runs obng-prison-tutorial-alternate/20261124-215336 and -215012): scene pass 7.3 -> 3.0 ms, work 11.3 -> 6.7 ms.

- **What moved**: of 1 075 geometries the chooser was asked for, 789 were ShadowLightShader's and went to
  Lighting30Shader (21 SkinShader's and 19 ParallaxShader's stayed, 68 were Lighting30Shader's already, 178
  others). The eye pass's records with the probe: 3XLIGHTING_Vc 441, 3XLIGHTING 62, 3XLIGHTING_S 54, 3XLIGHTING_G
  11, 3XLIGHTING_VcS 8; SkinShader's chain about 30 a frame.
- **The picture is close, not the same.** The still room, the left eye at 31 marks (`tools/re/run-pictures.py`):
  1.55 % of the pixels differ between the runs (median; 0.5 - 0.7 % from mark to mark inside one run, the torches).
  The dark corridor near the camera is the same within 2 % (the floor 7 % darker); the lit room's stone is 5 - 15 %
  darker with the probe (the arch's mean colour (60 - 65, 63 - 66, 49 - 51) against (54 - 58, 59 - 62, 47 - 49)).
  The actors stand elsewhere in the two runs (the script stops the AI twelve seconds in), so this is no pixel
  gate. The fight's films show the same look at their size (640 pixels wide, the right eye). Sheets:
  artifacts/evidence/lighting30-still-d14-room.png, lighting30-fight-h03.png. **Why the stone is darker is not
  read** (candidates: the specular term, the sums clipped between the passes of the chain, the fog).
- **No actor shadow, as read**: the GPU's time for the actor shadows' stage falls from 0.36 to 0.04 ms.
- **No fault in six runs** (three with the probe: -214421, -215012, -215816): no crash, no "SHADER ERROR" line.
  Single-pass stereo draws both eyes of the moved geometry (the dumps are the left eye, the films the right).
- **Derived for Windows** (not measured there): the fight's own work was 15.6 ms against a period of 11.1; with the
  same share (-45 %) it is about 8.6 ms.

**Not measured or not built yet**: outdoors (the projected shadow's variants, the sun; draw instancing replaces
SLS2003 only, so moved geometry is not instanced there); SkinShader's and ParallaxShader's geometry (bits 2 and
4); a geometry under more than eight lights (the ninth and further are dropped); a long session; Windows and the
look in the headset. For a route the list needs its actor shadow records (0x14E .. 0x151, as the chain's builder
adds 0x177 ..: a record a shadow light, read at the end of 0x0085AE20) and a picture gate in a place without
actors.
