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
- **The picture of these runs is no gate** (corrected the same evening): the script stopped the AI twelve seconds
  after the load, so the people - the torch carriers among them - stood elsewhere in the two runs, and the
  "lit stone 5 - 15 % darker" read from them was their torches, not the shader. The gate is the section "The
  picture gate" below. The fight's films show the same look at their size (640 pixels wide, the right eye;
  artifacts/evidence/lighting30-fight-h03.png).
- **No actor shadow in these runs, as read**: the build had no shadow records yet (below).
- **No fault in six runs** (three with the probe: -214421, -215012, -215816): no crash, no "SHADER ERROR" line.
  Single-pass stereo draws both eyes of the moved geometry (the dumps are the left eye, the films the right).
- **Derived for Windows** (not measured there): the fight's own work was 15.6 ms against a period of 11.1; with the
  same share (-45 %) it is about 8.6 ms.

**Not measured or not built at that point**: outdoors, the actor shadow records and a picture gate (all three in
the sections below); SkinShader's and ParallaxShader's geometry (bits 2 and 4); a long session; Windows and the
look in the headset.

## Measured outdoors: the Gold Road (2026-11-24)

`tools/hand-scripts/obng-profile-gold-road-single-dreamair.txt` (save OBVRHarnessMOO, three spots, at each the head
through eight directions and a walk east), the deployed OBNG.dll 852878c0, the routes as played, one run a side:
obng-profile-gold-road-single-dreamair/20261124-222251 (the game) and -221838 (`[Probe] Lighting30Kinds=1`, 2 098 of
2 648 geometries moved).

| phase | scene pass ms (p90), the game | the probe | own work ms, the game | the probe |
|---|---:|---:|---:|---:|
| spot 1, the head's sweep | 2.2 (3.0) | 2.2 (2.8) | 5.2 | 5.0 |
| spot 1, the walk | 2.9 (3.1) | 2.7 (2.9) | 6.0 | 5.4 |
| spot 2, the sweep | 2.6 (3.0) | 2.7 (3.2) | 6.5 | 7.1 |
| spot 2, the walk | 2.6 (2.8) | 2.4 (2.7) | 6.4 | 6.6 |
| spot 3, the sweep | 3.1 (4.0) | 3.2 (3.7) | 6.2 | 6.0 |
| spot 3, the walk | 2.6 (3.1) | 2.4 (2.9) | 6.2 | 5.1 |

**No gain and no loss outdoors**: the differences are within what two runs of this scenario differ by. Over the
whole run the eye pass has 936 draws with the game (max 1 270) and 723 with the probe (max 1 038), the GPU 4.6
against 4.3 ms. Draw instancing groups nothing with the probe (0 groups against up to 7 475 a window): it replaces
SLS2003's programs only, and the moved geometry draws with SM3's - one record a geometry does about what the
instancing did. An instanced SM3 vertex program would be the next step there (not built). No fault in the run.

The outdoor picture, looked at afterwards (obng-profile-gold-road-eyedump/20261124-230907 the game, -230445 the
probe; the eye dumps of both at four marks in artifacts/evidence/lighting30-road.jpg): rocks, the castle, tents and
trees are lit from the same side and look alike in both. No pixel comparison there - after a `cow` two runs' cameras
differ, the weather and the people on the road too (one run has rain at the second spot, the other a group of
walkers at the third). The sepia tone is the save's look with this mod list, in both runs.

## More of the two paths, read for the route (2026-11-24)

- **The lights of one record.** The shader's setup caps a record's light count at 8 (0x00B2DCFC), and the pixel
  programs cap it again by their own loop bound: in the package in use (004, ps_2_a builds) seven lights without
  specular (SM3000.pso, `LightData` x14), eight with (SM3001.pso, x16), four in the SM3LL programs, which the
  picker 0x00862AD0 takes when the light count is at most the value of 0x00B2DD00. The packages 009, 018 and 019
  hold the same programs built for vs_3_0 / ps_3_0 with `LightData` x20 - ten lights. A geometry with more lights
  than its program draws loses the last ones of its list; ShadowLightShader's chain draws them all.
- **The same specular flag.** Both properties get their flags from the same setup (0x007DA220; the one-pass
  property's 0x00864400 calls it and then drops the geometry's property of type 7) and the same texture setter
  (0x007D8160): bit 0, specular, is set when the normal map's format has an alpha channel (the gloss) or the
  geometry has an environment map; bit 0x10 the same for a second texture layer. The chain adds specular passes
  for bit 0 or bit 0x10, the one-pass shader's setup reads bit 0 alone - the same for a geometry of one layer.
- **Specular does not fade here.** The chain's getter 0x007D85D0 fades a property's specular between
  fSpecularLOD1 and fSpecularLOD2 and leaves its passes out beyond; the one-pass getter has no such fade (its
  fade is the environment map's), so a moved geometry keeps its specular at any distance. Not changed.
- **The chain's light formula is the one-pass program's.** SLS2031.pso (BSSM_AD3): the normal map, the first light
  as a direction, two point lights with `1 - f(xy) - f(z)` from an attenuation map, each light's `N.L x colour`
  clamped, ambient added, no texture; SM3000.pso: the same with `1 - (d / r)^2` computed. What differs is where
  the sum is clipped: the chain writes the light sum into the target (clipped at 1) before the texture pass
  multiplies it, the one pass multiplies first.
- **How the chain adds an actor's shadow** (instructions 0x0085BAAD - 0x0085BB7B, the builder's end): when
  0x00405A80 says the game draws actor shadows (render path 3 or more and bit 0x10 of the pass mask), every light
  of the property (0x007ED1A0 first, 0x007ED3B0 next) with the byte +0xF4 set gets a record - 0x00401F00(0x10),
  0x007E2370(memory, geometry, 0, 0, 1, light), pass id 0x177 (0x178 skinned, 0x179 land, 0x17A one subclass),
  byte +7 = 1 - put at the list's head (0x006AA320 on property +0x28; the list is head +4, tail +8, count +0xC).
- **The one-pass shader can draw such a record**: its setup 0x007FF4A0 handles the pass ids 0x14E .. 0x151 - it
  takes the record's first light, and for a light with +0xF4 set binds the shadow map (0x00405A30) and writes the
  light's values into c7 - and the picker has their programs (0x00B47460 ..; SM3023.pso: the shadow map sampled
  sixteen times, a factor that darkens what is there). Only the builder never makes one.
- **Both Windows and Linux run the same path**: RendererInfo.txt of both games reads RenderPath BSSM_SV_2_A,
  "3.0 Shaders: yes", Shader Package 4.

## `[Probe] Lighting30Shadows` and the probe's mark (built 2026-11-24)

`src/engine/render/Lighting30ShadowLogic.h`, tests/Lighting30ShadowLogicTest.cpp, in plugin/src/Lighting30Kind.cpp.

- **The mark.** A moved geometry's property is now made inside the probe - the game's own object preparation
  0x007B7FC0 called with kind 0x1A for that geometry - and the word at +0xFC of the property set to 'OBNG'. That
  word is the fourth component of the hair tint: the constructors zero it, a clone copies it (0x00863540), the hair
  setup writes 1.0 there (0x005551C0) and no hair pixel program reads it (c2.xyz only). The caller's own attach
  then finds a property of kind 10 and leaves it (0x007FF350's first lines); the probe hands the preparation's
  answer back as the kind or 0, so that the caller's result is the one the game would have had.
- **The records.** The list builder's call that adds the lighting record (0x00864078, `call 0x006AA320`) goes
  through the probe: for a marked property whose lighting record is no hair and no FaceGen record it adds, as the
  chain's builder does, a record a shadow light - pass id 0x14E, 0x14F with vertex colours, 0x150 skinned, 0x151
  both (from the lighting record's own id). The game's own Lighting30 geometry carries no mark: one load and a
  compare more per list built, nothing else.

## The picture gate: a still room, the AI stopped before the load (measured 2026-11-24)

`tools/hand-scripts/obng-lighting-still.txt`: the tutorial's first room (save OBNGPrisonTutorial) with `console-menu
tai` - nobody moves between the load and the pictures -, the game's time stopped, both eyes dumped at twelve marks
two seconds apart. `tools/re/run-pictures.py <run A> <run B> [--region name=x0,y0,x1,y1 ...]` compares the same
mark of two runs: the share of pixels that differ by more than 8 of 255 (after a 4x reduction) and the mean colour
of named regions. Test build dc68d66b, the routes as played, runs obng-lighting-still/20261124-:

| run | what |
|---|---|
| -223953 (A1), -224454 (A2) | the game, twice |
| -223723 (B) | `Lighting30Kinds=1`, `Lighting30Shadows=1` |
| -224224 (B0) | `Lighting30Kinds=1`, `Lighting30Shadows=0` |
| -225023 (A'), -225253 (B') | the game and B with bDoSpecularPass=0 |

- **What two runs of the game differ by** (A1 against A2): 0.18 % of the pixels (median of 16 marks, max 1.47);
  surfaces no torch reaches are the same to 0.1 %; torch-lit surfaces differ by up to 15 % at one mark (the
  flames' phase) and by at most 1.4 % in the median over the marks. So the scenario is a gate for everything but
  one mark's torch light.
- **The game against the one-pass lighting** (A1, A2 against B): 0.48 - 0.57 % of the pixels differ. By region
  (the median over the marks, B against A): the dark walls -3.6 % (0.2 of a grey level at their brightness of 6),
  the far room behind the lit one +2.7 %, the lit floor -11 % (four pairs of runs: -10.6 to -12.4), the lit arch
  +3 to +5 % (inside the flames' range), the lit pillar the same (-0.2 %, +2.6 %).
- **The actor shadows are there with the records**: on the floor beside the torch carrier the game has (59.7,
  52.8, 24.7), B (59.5, 52.7, 24.6) and B0 (64.1, 56.8, 26.5) - 7 % brighter without the records. Where a shadow
  falls is the same in the game and in B (artifacts/evidence/lighting30-still2-shadow-footprint-d07.png: what
  each darkens against B0). On the dark floor by the near guard the one-pass shadow is a little stronger (the
  floor at 7.5 against the game's 8.0, 8.1 without a shadow): SM3023.pso takes fifteen samples of the shadow map
  with a depth bias of -0.0039, the chain's programs (SLS2076 / 2078 / 2080) one, four or seven with -0.0002; the
  darkening by 0.6 of the light's colour is the same.
- **Specular is not the difference**: without the specular pass the regions differ as before (the walls -3.6 %,
  the lit floor -11.9 %, the far room +3.7 %), and taking it out changes both paths alike (the far room -4.3 % in
  the game, -3.3 % in B; the lit arch -4.7 and -3.8 %).
- **What is left has a shape and a cause** (artifacts/evidence/lighting30-still2-ratio-nospec.png, B' against A',
  the mean of twelve marks): the floor around the torch carrier is darker with the one pass, the arches above the
  same or brighter, surfaces no point light reaches the same. Read: **the chain turns a point light's direction
  into a unit vector at every vertex and lets the rasteriser interpolate it** (SLS2029.vso: `add r0, -v0, c16 /
  nrm r1, r0 / dp3 oT1.x, v1, r1 ...`; the pixel program normalises what arrives), **the one pass computes it at
  every pixel from the interpolated position** (SM3000.pso: `add r7.xyz, <light>, -t6`). On a small triangle the
  two agree. On a large floor polygon with a torch low above it the interpolated unit vectors point more upwards
  than the true direction - a light a tenth of the polygon's length in from one end and as high above it gives
  N.L 0.94 in the polygon's middle against a true 0.24 - so the chain lights such a floor more than the light's
  place allows, and the one pass shows the falloff. Checked and not it: the lights taken (the same iterators' answers), their colours
  (0x007EDCD0 and 0x007ED6C0 are the same formula), the radius (NiLight +0xF8 in both), the first light's
  direction (negated and turned into the geometry's space the same way, 0x007FB6F0 and 0x007C9230), the tangent
  space (unit length and orthogonal in five of the dungeon's meshes), the attenuation (`1 - d^2 / r^2` in both,
  by two texture reads there, computed here), the vertex colours (both multiply the texture by them).
- The scene pass in this room: 4.2 ms (p90 4.7) with the game, 2.7 (3.1) with B, both with every frame's eye read
  back.

**The fight with the shadow records** (obng-profile-prison-fight-hold/20261124-224725, the same build): scene pass
2.9 ms (p90 3.4), own work 4.8 ms, 813 draws an eye pass, 696 records, the GPU 4.35 ms - 0.2 ms and 120 records
more than without them (-214421), against the game's 6.2 ms, 8.2 ms and 2 291 draws. 12 576 shadow records in
200 856 lists built (1 310 lists a second in the hold: the torches' flicker clears the lists' keys, as it does for
the chain); no list had more lights than its program draws (the most on one record 7). No fault in the seven
runs of this build.
