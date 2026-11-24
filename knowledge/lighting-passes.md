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
