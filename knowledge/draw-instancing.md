# Draw instancing: what Oblivion's draws share and what an instanced path would carry

Measured 2026-11-12 on Linux (DXVK, the Windows mod list, single pass, the culler off) with OBNG's instance
count (`[Route] StereoDrawTimer`; `plugin/src/InstanceGroupLogic.h`, `plugin/src/StereoDrawTimer.{h,cpp}`);
the numbers per run in `docs/performance-backlog.md` ("U1", "V1", "W1").

## How many draws could be one

Per eye pass, keyed by the geometry's model data (+0xB4) and the pass object (shader+0x3C): 34-39 % fewer draws at
Anvil's docks (1 078-1 113 pass setups a pass), 39-44 % in the Imperial City's Market District (758-979); groups up
to 12 and 25-30. (measured)

## What differs between copies of a mesh

Within a group of one mesh and pass (measured; property slots by type as knowledge/shadow-light-list-remove.md
names them, layouts from xOBSE's NiProperties.h / NiNodes.h):

- the same: textures (texturing property, 98-100 %), the shader property's class and flags (100 %), the programs
  (100 %), mostly the lights (89-100 %);
- an object of its own each, mostly equal in value: the material (values differ in 16-35 %) and the shader property
  (its alpha differs in 16-35 %) - the alpha of objects fading in and out (derived);
- per object anyway: the world transform, and with it everything the engine uploads in model space
  (ShadowLightShader::SetupTransformations 0x007C9230, knowledge/culling-and-draw-setup.md: the inverse world, the
  lights' model-space positions 0x007C9140, the shadow projection, the packed world).

## Which programs

The grouped draws' pass objects: 2-4 cover 80 %, 3-6 cover 90 %. Their programs, by the bytecode the device holds
(IDirect3DVertexShader9 / IDirect3DPixelShader9::GetFunction, FNV-1a) matched against the entries of
`Data/Shaders/shaderpackage0NN.sdp` (format: three dwords - 100, the count, the data size - then per entry a
0x100-byte name, a dword size and the bytecode); disassembled with vkd3d-compiler (`-x d3dbc -b d3d-asm`):

| share of the grouped draws | vertex program | pixel program | vertex constants read | pixel constants read |
|---|---|---|---|---|
| 56-81 % | SLS2003.vso (vs_2_0, 33 instructions) | SLS2003.pso (ps_2_1, 22, 4 samplers) | c0-c3, c13, c23, c24, c28, c29, c31-c33 | c1, c2, c7 |
| 9-30 % | SLS2073.vso / SLS2075.vso (22) | SLS2080.pso (88, 1 sampler) | c0-c3, c8-c11, c16, c23 | c0-c6 |
| 4-6 % | SLS2013.vso (43) | SLS2018.pso (32) | c0-c3, c13, c23-c25, c28, c29, c31-c33 | c1-c3, c7 |
| 3-6 % | PAR2002.vso (41) | PAR2002.pso (28) | as SLS2013 | c1, c2, c7 |
| up to 4 % (town) | SM3004.vso (33) | SM3LL011.pso (185) | c0-c3, c15, c16, c21, c22, c24-c26 | c0-c18 |

Read in SLS2003.vso's instructions (the meaning derived from the use, not from the constant map's ids): c0-c3 the
model-view-projection rows (oPos), c13 a direction dotted with the tangent, binormal and normal (a light direction in
model space), c23.xy the fog's start and range (`(c23.x - |pos|) / c23.y` into oD1.w), c24 the fog colour (oD1.xyz),
c28/c29/c31 three rows of a matrix on the model position and c32/c33 scale and offset - texture coordinates oT7 the
pixel program samples s6/s7 with (a projected shadow or canopy map). The pixel program: c1.w scales the alpha, c1.xyz
and c2 colours, c7 two switches.

## What an instanced path would take (derived)

Per instance the world transform (from which the vertex program derives model-view-projection, the model-space light
direction and the oT7 matrix, with the view-projection, the world-space light direction and the shadow matrix given
once) and an alpha; the fog and the light colours once. That means instanced versions of the vertex programs above
and pixel programs that take the alpha from an input instead of c1.w - for SLS2003 alone a 33- and a 22-instruction
program, covering most of the gain - plus a draw path that collects a pass list's draws of one mesh and pass into
one call with a per-instance stream (SetStreamSourceFreq). Not read yet: where c28-c33 are computed (0x007C9230's
step 7 names the shadow projection 0x00B45498 and the packed world 0x00B45560) and whether the material and shader
property's alpha are the only per-instance values the pixel side reads.

## The constant tables, and what the order of draws allows (2026-11-12)

The programs keep their D3DX constant tables (CTAB, read with a small parser over the bytecode): SLS2003.vso -
ModelViewProj c0x4, LightDirection c13, FogParam c23, FogColor c24, ShadowProj c28x4, ShadowProjData c32,
ShadowProjTransform c33; SLS2003.pso - AmbientColor c1, PSLightColor c2, Toggles c7, BaseMap s0, NormalMap s1,
ShadowMap s6, ShadowMaskMap s7 (and ps_2_b, not ps_2_1 as vkd3d prints it); SLS2073.vso - ModelViewProj c0x4,
ObjToCubeSpace c8x4, LightPosition c16, FogParam c23; SLS2080.pso - PSLightColor c2, ShadowMap s6. The pixel
program's alpha is `BaseMap.a x AmbientColor.w` (read).

Hardware instancing in D3D9 needs a vs_3_0 vertex program (Microsoft Learn, "Efficiently Drawing Multiple Instances
of Geometry (Direct3D 9)": "This technique requires a device that supports the 3_0 vertex shader model"; the DirectX 9
SDK's "Vertex Stream Frequency": stream frequency "is ignored ... if the current vertex shader set is less than version
3_0"), and a hardware vs_3_0 pairs only with a ps_3_0 (the shader model 3 rules as quoted on Stack Overflow
13913691, not read in Microsoft's own page) - so both programs are rewritten, not patched.
`plugin/shaders/SLS2003Instanced.hlsl` is that pair, translated from the disassembly line by line, the model-space
constants rebuilt from a per-instance world transform; vkd3d-compiler builds it (vs_3_0: 153 instructions against the
original's 33 - vkd3d does not fold the matrix products; fxc would, not available on Linux).

Order (measured, runs `obng-profile-anvil-docks/20261112-162150`, `obng-profile-market/20261112-162422`): in draw
order a pass has 134-182 (docks) and 281-298 (town) runs of consecutive draws with one pass object, the longest
118-160 draws; grouping only inside a run leaves exactly as many groups as grouping over the whole pass (39-40 %
fewer draws at the docks, 50-51 % in the town). So an instanced path that only reorders inside a run - which keeps
every additive pass after the pass it adds to - loses nothing. (Skinned and direct draws between two whole draws are
not in the count: an upper bound.)
