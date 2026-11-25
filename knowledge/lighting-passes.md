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
the plugin's start is concerned - all of them but the terrain's blocks, which the probe leaves to the game since
2026-11-25 (build 9d91c7bf; "The picture gate outdoors" below). It is a measuring instrument: the picture
changes. (As first built, moved geometry received no actor shadow; the shadow records came the same day, below.)

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

**Read this section with two corrections of 2026-11-25**: these runs moved the terrain too, which the one pass
draws wrong (one texture, no LAND pass), and their pictures were taken behind an open menu - so "no gain and no
loss" is a statement about the frame's cost with a wrongly drawn ground, and nothing in this section says what
the one pass looks like outdoors. The outdoor numbers and pictures that hold are in "The picture gate outdoors"
below.

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

**Menus in these two runs** (OBVR.log's `Hands: menu` lines against the marks, read 2026-11-25): after every `cow`
Nadi's mod list opens a Message menu and, once that is closed, a Book menu; the scenario's CloseAllMenus at each
mark closes them. The first spot's sweep had a menu open for about five of its 23 seconds in both runs, and the
probe's run a Dialog and a Generic menu over most of the first spot's walk - that phase is not a comparison. The
second spot is free of menus in both runs and the third nearly (two seconds in the game's run): the statement
above rests on those.

The outdoor pictures first taken (obng-profile-gold-road-eyedump/20261124-230907 and -230445,
artifacts/evidence/lighting30-road.jpg) are no evidence: that scenario closes no menu, so the Message menu stayed
open, the world was paused and OBVR's menu shade (Render.MenuShade, colour FFE3B2) lay over it - the sepia tone
of those pictures, which this file first called the save's look (corrected by Nadi, 2026-11-25). The outdoor
picture gate is `tools/hand-scripts/obng-lighting-road-still.txt` (below). `tools/re/run-menus.py <run> [--from
<mark> --to <mark>]` lists a run's open menus against its marks and fails when one lies between the two marks.

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

**The fight again with the deployed build 9d91c7bf** (2026-11-25, one run a side, OBVR.dll b7645e28, the DLL of
OblivionWalkers parked): the game 6.1 ms (p90 6.8) of scene pass and 8.0 ms of own work (-093610), the one pass
3.2 ms (3.7) and 5.1 ms (-093312; 783 geometries moved, no land block in the dungeon). The day before it was 2.9
and 4.7 ms with another OBVR.dll; one run does not say whether the 0.3 ms are that. In 216 568 lists built for
moved geometry 12 had eight lights on a record, one more than the program without specular draws - the first
time a light is left out in a measured run. The flags counted on moved geometry there: 0x1 in 166 089 lists, 0x2
in 118 509, 0x80 in 18 213, 0x100 in 21 943, 0x200 in 6 782, 0x20000 in 16 507, 0x200000 in 1 482 - none of the
three bits only the chain's builder reads. (A first try of the game's run, -092604, ended in the runner's timeout
"no 'OBVR ready' line within 240.0 s": the game had loaded the save and drawn 5 739 scene passes, but the run's
folder has no OBVR.log - handed to the OBVR session.)

## The picture gate outdoors: the Gold Road's camp from a fixed save (measured 2026-11-25)

`tools/hand-scripts/obng-lighting-road-still.txt` from the save OBVRHarnessRoadStill
(`tools/hand-scripts/obng-make-road-still-save.txt` writes it): the camp in the cell -21 -1, the AI stopped before
the load, noon, the weather forced clear (`fw 38eee`), the game's time stopped, the menus closed, six pictures
looking east (the marks d01 .. d06) and six with the head turned a quarter, to the camp (e01 .. e06).
`tools/re/run-menus.py <run> --from d01 --to e06` says whether a menu was open (none in the runs below);
`tools/re/run-pictures.py A B --marks e0 --region ...` compares one view. Runs obng-lighting-road-still/20261125-,
Linux, the stand-in headset, the routes as played, the Windows mod list.

**Why a save.** The camp is filled from levelled lists when its cell loads. Four runs that went there with `cow`
(-082341, -082620, -083644, -083947, the script's first version) met a robed figure and a second one, the robed
one alone, three bandits, a dog - and a region of two runs' pictures could not be compared for sure. The save
holds one group: two people, one with a burning torch.

**What two runs of the game differ by** (-085459 against -085756): 15 % of the pixels looking at the camp and
36 % looking east - the grass is planted anew at every load and the trees sway, so that share says nothing here.
The regions on what stands still agree to 0.1 %: the tent's roof, the near rock's top and side, the fallen log,
the rack's post, a tree's trunk, the sky (the big rock to 0.3 %). The ground by the torch moves by up to 2.6 % at
one mark - the flame.

**The probe drew the terrain wrong until that day; these pictures found it.** With the builds before 9d91c7bf a
frame with the probe had no LAND pass (the game's frame there: LANDAD_AShp 210 records, LANDAD_Shp 36, LANDLO_A
36, LAND2xDIFF_A 13) and the ground was one bright texture instead of the land's blended layers (-082341,
-083947; artifacts/evidence/lighting30-road-terrain.jpg). Read: the land's builder 0x004C2630 hangs a block's
texture shares a vertex on the block's node as the extra data "tex %" (0x004C2763 .. 0x004C277C) and prepares
the block with kind 1 like any geometry (0x004C2790); the lighting property's geometry step 0x007D7F80 takes the
shares from the geometry's parent, keeps them at the property's +0xD8 and sets the property's flag 0x4000; and
ShadowLightShader's list builder 0x0085AE20 sends a property with that flag to its land branch (0x00854380, the
passes 0x048, 0x049, 0x168 .. 0x176). Lighting30's builder 0x008637D0 does not read the flag. **Since 9d91c7bf
the probe leaves a geometry whose parent carries "tex %" to the game** (`IsLand` in plugin/src/Lighting30Kind.cpp,
`KindFor(..., land)` in the logic header): in -085231 100 land blocks stayed the game's, 686 geometries moved,
and the LAND passes are back with the game's counts.

A first attempt missed, and its count is a finding: build d99a90ab left to the game every geometry that came to
the chooser with a lighting property of kind 5 .. 9 (ShadowLightShader's attach 0x0080B190 keeps a property of
kind 5 .. 10, Lighting30's 0x007FF350 only kind 10) - and met none in three scenes (0 of the 695, 769 and 783
choices it would move: -083947, obng-lighting-still/20261125-084227, obng-profile-prison-fight-hold/20261125-
084457; none came with kind 10 either). The preparation 0x007B7FC0 asks the chooser only for a geometry that
lacks a property or a shader, and attaches one that has both to its shader again without asking; the land's
property is made by ShadowLightShader's attach like any other and filled by the land's code afterwards.

**Surfaces in the noon sun are brighter with the one pass** (9d91c7bf, two runs a side from the save: -085459 and
-085756 the game, -085231 and -090021 the one pass; B against A, the median of six marks, for each pair):

| region (looking at the camp unless said) | the game, mean colour | the one pass | B against A |
|---|---|---|---:|
| the near rock, its top | (63.0, 68.9, 57.7) | (86.7, 92.9, 79.9) | +36.9 %, +37.2 % |
| the near rock, its side | (76.2, 79.5, 67.8) | (89.1, 92.4, 82.2) | +17.8 %, +18.5 % |
| the tent's roof | (81.4, 80.3, 68.1) | (84.8, 84.8, 77.1) | +7.3 %, +7.5 % |
| the big rock (looking east) | (42.4, 50.9, 49.7) | (43.5, 52.0, 51.9) | +3.1 %, +1.7 % |
| the fallen log | (50.9, 60.2, 44.2) | (50.9, 60.2, 44.2) | 0.0 %, 0.0 % |
| the rack's post | (74.2, 76.3, 63.2) | (73.4, 75.6, 62.8) | -0.8 %, +0.2 % |
| the ground by the torch (terrain, the game's in both) | (78.6, 76.0, 52.4) | (78.1, 75.6, 52.2) | -0.6 %, +0.6 % |
| a tree's trunk, the sky (looking east) | | | 0.0 % |

artifacts/evidence/lighting30-road-still.jpg (both views side by side), lighting30-road-rock.jpg (the rock and
the tent at full size), lighting30-road-ratio.jpg (where the one pass is brighter).

- **Not specular and not the trees' canopy shadow**: with bDoSpecularPass=0 on both sides (-090521 the game,
  -090255) and with bDoCanopyShadowPass=0 on both sides (-090817 the game, -091042) every region has the numbers
  above to 0.2 %. (`GAME_INI="Section:Key=Value ..."` of tools/measure-fight.sh sets such a key for a run.)
- **Read: the chain clips the light sum before the texture, the one pass does not.** SLS2003.pso - ambient, the
  first light, the texture and the projected shadow in one pass, the program of a geometry under the sun (the
  draw instancing's own finding, above) - ends its light sum with `mad_sat_pp r2.xyz, r3, r1, c1`: the first
  light's N.L x colour x shadow plus the ambient colour, clipped at 1 a channel, and only then multiplied by the
  texture. So do the chain's other first passes (one or two clipped sums in SLS2000, 2001, 2004, 2012, 2018,
  2031). SM3000.pso, SM3010.pso (the one with the projected shadow) and SM3LL000.pso add the ambient colour and
  all lights with `add_pp r1.xyz, r2, r1` and multiply by the texture unclipped; only the target clips the
  product. Where ambient and sun together exceed 1 - a surface that faces the noon sun - the one pass is
  brighter by that excess, a texture shown beyond its own colour; where they stay below 1 - the log's and the
  post's steep sides - the two paths give the same.
- **Confirmed at night** (the same script with `set gamehour to 1`, obng-lighting-road-still-night/20261125-092335
  the game, -092107 the one pass; the DLL of OblivionWalkers out of the game by then): the near rock's top
  +1.4 %, its side +2.1 %, the tent's roof +1.6 % - inside what the torch's flame moves there at night (-1.9 to
  +4.7 % from mark to mark) -, the log +0.1 %, the post +0.4 %, the big rock +2.5 %. At night the light sums
  stay below 1 (derived: the brightest of these regions is at 32 of 255), and the noon's differences are gone.
- **Derived from the noon's numbers**: a region's ratio a channel is the unclipped light sum itself - the top's
  (1.38, 1.35, 1.39), the side's (1.17, 1.16, 1.21), the roof's (1.04, 1.06, 1.13): more excess the more a surface
  faces the sun, and a bluish excess where the sun's part is small, as a bluish ambient colour gives.
- **What it means for a route**: with the one pass as it is, objects in the sun are brighter than the game shows
  them and than the terrain they stand on, which stays the chain's. Matching the chain takes a clip in the
  one-pass pixel programs - at least min(1, ambient + first light); whether the further lights belong inside it
  differs by the chain's pass family (BSSM_AD3 sums three lights before its texture pass, ADT2 two, more lights
  come by passes of their own) and has to be read first. The programs come out of the shader package, so that
  is a change of their bytecode where the game loads them. Built the same day: "`[Probe] Lighting30Clip`" below.

**The frame's cost in this scene** (the phase d01 .. e06, both eyes dumped at every mark in it, so slower by
itself; `[Probe] GpuPasses`: the mean of the summaries from the third on):

| run | scene pass ms (p90) | records an eye pass | GPU ms a frame | GPU ms, the scene's target |
|---|---:|---:|---:|---:|
| -085459 the game | 2.7 (3.1) | 789 | 4.32 | 2.69 |
| -085756 the game | 2.8 (3.2) | 789 | 4.20 | 2.61 |
| -085231 the one pass | 2.9 (3.2) | 733 | 4.20 | 2.49 |
| -090021 the one pass | 2.9 (3.3) | 732 | 4.25 | 2.57 |

Nothing gained and nothing lost outdoors by day, as far as two runs a side can say: 56 records fewer of about
790, the scene pass 0.1 to 0.2 ms more, the GPU the same. The terrain, the trees and the grass are most of this
frame, and none of them is the probe's.

**The flags of moved geometry** (`Probe: Lighting30 flags`, counted as each pass list is built, -085231): of 4 573
lists 0x1 in 2 988, 0x2 in 1 402, 0x80 in 396, 0x100 in 446, 0x2000 in 231, 0x20000 in 394, 0x400000 in 49. Of
the three bits only the chain's builder reads, 0x1000 and 0x4000 (the terrain) do not occur on moved geometry
any more; **0x400000 does, in 49 lists - not read yet what the chain does with it.**

**OblivionWalkers was loaded in every run of this file up to here**, its ESP through the Windows mod list and
its DLL from the Linux game's plugin folder (the Walkers session's note of 2026-11-25: the DLL had been back in
the Linux game since 2026-11-20). The DLL was parked at 09:13:22 that day; the night pair above and the runs
after it are without the DLL, the ESP still in the list. Both sides of every comparison here had the same state.

## `[Probe] Lighting30Clip`: the light sum clipped at 1 (built and measured 2026-11-25)

`src/engine/render/Lighting30ClipLogic.h`, tests/Lighting30ClipLogicTest.cpp, plugin/src/Lighting30Clip.{h,cpp};
`tools/re/sm3-clip.py` applies the same rule to a package file. Nadi's choice of that day: the clip for
everything the one-pass shader draws, the game's own hair and heads too, not for moved geometry alone.

- **What the chain's sums hold** (read, package 004): SLS2003.pso `mad_sat_pp r2.xyz, r3, r1, c1` - the first
  light x the projected shadow + ambient; SLS2012.pso (two lights) `add_sat_pp r2.xyz, r1, c1` - both lights +
  ambient; SLS2005.pso (glow) `mad_sat_pp r2.xyz, r4, r2, r1` with r1 = ambient + emittance x glow map. So the
  chain's first pass is min(1, ambient + its lights [+ glow]) times the texture, and BSSM_AD3 writes its clipped
  sum of three lights into the target for the texture pass to multiply. One saturate on the one-pass program's
  whole sum is the same thing for a geometry whose lights the chain's first pass holds (one or two outdoors,
  three indoors). Not followed through: a geometry with more lights than that, where the chain adds the rest
  by passes of their own.
- **Where the programs are made** (read in the instructions; the function is missing from the decompile's
  export): Lighting30Shader's program loader 0x007FD560 prints "SM3%03i.pso" and "SM3LL%03i.pso" and calls the
  pixel program maker 0x00801C90 at 0x007FEAF0, 0x007FEB29 and 0x007FEBBC - a thiscall with six arguments (path,
  macros, profile, name, 0, 0; `ret 0x18`). The maker looks the name up in the loaded shader package
  (0x007DAC70 on the object at 0x00B430B8), takes the bytecode at the entry's +0x104 (the entry: a 0x100-byte
  name, the length, the bytecode - the file's own layout) and gives it to the device's CreatePixelShader
  (slot +0x1A8). The probe stands in the three calls and sets one bit in the package's bytecode before the
  maker reads it; all three calls are redirected or none.
- **Which bit**: the saturate modifier (bit 20) of the destination of the add that completes "ambient plus
  lights" - found by the program's constant table, not by a place: the first instruction that takes the constant
  named AmbientColor into a temporary's xyz, then the first `add` that writes that temporary's xyz from itself
  and another temporary; a rework of the temporary from itself in between (the glow variant) is passed, anything
  else that writes it ends the search without a change. One line a program changes, e.g. SM3000.pso `add_pp
  r1.xyz, r2, r1` -> `add_sat_pp r1.xyz, r2, r1` (`sm3-clip.py <package> --diff SM3000.pso`).
- **What it finds**: in package 004 (both games') one add in each of the forty lighting programs - SM3000 ..
  SM3019 and SM3LL000 .. SM3LL019: plain, specular, hair, FaceGen, glow, each with and without the projected
  shadow - and nothing in the nineteen others, which have no AmbientColor; the game's log of every run says the
  same ("59 pixel programs made by the shader's loader: 40 clipped ... 19 without the constant AmbientColor").
  Packages 007 and 013 the same. In the packages built for ps_3_0 (009, 018, 019) the twelve programs without
  specular have another shape (the temporary is written anew before a sum is made of it) and are left as they
  are - 28 of 40 clipped there; neither game runs those.

**Measured outdoors at noon**, the camp from the save, build a463717f, one run each: -100648 the game, -100914
the one pass with `Lighting30Clip=0`, -100421 with the clip (the same build for all three, OBVR.dll 20dfcfd2, the
Walkers DLL parked). Against the game, the median of six marks:

| region | without the clip | with the clip |
|---|---:|---:|
| the near rock, its top | +36.8 % | 0.0 % |
| the near rock, its side | +17.9 % | 0.0 % |
| the tent's roof | +7.3 % | +0.3 % |
| the boots of the woman with the torch (moved, skinned) | +6.9 % | -0.3 % |
| the fallen log | +0.1 % | +0.1 % |
| the rack's post | -0.8 % | +0.4 % |
| the ground by the torch (terrain) | +0.4 % | -0.2 % |
| her face (the game's own one-pass geometry in all three) | -0.1 % | -0.2 % |
| the other woman's hair (the same) | +1.7 % | +0.6 % |
| the skin of her back (SkinShader in all three) | +0.2 % | -0.3 % |
| the big rock, looking east | +3.1 % | +3.5 % |

artifacts/evidence/lighting30-clip-rock.jpg (the rock and the tent three ways), lighting30-clip-ratio.jpg (where
the one pass is brighter, without and with the clip), lighting30-clip-people.jpg.

- **The sunlit surfaces are the game's now**: to 0.3 %, where two runs of the game agree to 0.1 %.
- **The game's own hair and heads did not change here**: the face is the same without and with the clip, to
  the 2 to 3 % the torch's flame moves it from mark to mark - its light sum does not reach 1 in this scene.
  What a face in harder light does is not measured.
- **Left over, and not the clip's**: the big rock looking east is 3 % brighter with the one pass, with the
  clip and without, by day and by night (+2.5 %), with the specular pass and without it (+2.3 %); without the
  canopy shadow pass it was +1.6 % (the morning's runs -090817, -091042). So about half of it has to do with the
  trees' projected shadow. Not read.
- **The frame's cost is the same**: scene pass 2.9 ms (p90 3.4) the game, 3.0 (3.5) with the clip, 3.0 (3.5)
  without; the GPU 4.20, 4.27 and 4.22 ms a frame.

**Measured indoors**, the still room (obng-lighting-still/20261125-101410 the game, -101642 without the clip,
-101140 with it): **the clip changes nothing there** - without against with it 0.33 % of the pixels differ
(the flames), the far room 0.0 %, the dark wall +0.1 %, the torch-lit floor, arch and pillars inside the
flame's range (+1.9, +0.7, +0.2, -1.0 % in the median, -8 to +17 % from mark to mark). No light sum reaches 1 in
that room. Against the game the room is as it was the day before: 0.58 % of the pixels with the clip (0.64 %
without), the far room +1.8 %, the dark wall -3.7 %, the lit floor -6.7 % with these regions.

**The fight**, one run a side with a463717f: the one pass with the clip 3.2 ms of scene pass (p90 3.8) and 5.3 ms
of own work (-101913), the game 6.8 ms (7.6) and 8.9 ms (-102208). The one pass was 3.0, 3.2 and 3.2 ms in
that day's three runs; the game was 6.1 ms at 09:36 and 6.8 ms at 10:22 (another OBVR.dll in between, and the
fight is not the same load twice). In this run 888 of 202 983 lists built for moved geometry had eight lights on a
record, one more than the programs without specular draw.

## A long session with the one pass (measured 2026-11-25)

`tools/hand-scripts/obng-stress-ride-long.txt` (save OBVRHarnessMOO, fifteen minutes of flight over about sixty
cell borders), the deployed dc68d66b, `[Probe] Memory=1`: obng-stress-ride-long/20261125-073812 with
`Lighting30Kinds=1` and `Lighting30Shadows=1`, -080700 with `Lighting30Kinds=0`. Both PASS, no crash, no shader
error line. With the probe 14 812 geometries chosen for, 13 141 moved and marked, 138 054 pass lists built for
them, 1 089 shadow records, at most six lights on a record. This build still moved the terrain.

| | the game | the one pass |
|---|---:|---:|
| commit MB at the 30th memory summary | 740 | 766 |
| commit MB at the last (the 188th) | 860 | 884 |
| growth | +120 | +118 |
| address space MB at the 30th, at the last | 3 154, 3 346 | 3 095, 3 310 |
| the first leg's scene pass ms (p90) | 1.4 (1.9) | 1.4 (1.9) |

**The growth over a long flight is the game's with this mod list, not the probe's**: the two grow alike. The one
pass holds 7 to 38 MB more commit at the same summary (every twentieth compared: a median of 18), from the start
on. The address space reaches 3.3 of 4 GB in both.
