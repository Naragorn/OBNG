// The instanced counterpart of ShadowLightShader's pass with SLS2003.vso and
// SLS2003.pso (knowledge/draw-instancing.md): one draw for the copies of a mesh
// in one pass. The per-draw constants the engine uploads in model space are
// given once per draw here, taken to the space of the instance transform
// (plugin/src/DrawInstancingLogic.h Derive), and the instance transform comes
// from a second vertex stream.
//
// Shader model 3 throughout: D3D9 fetches a stream by instance only for a
// vs_3_0 program ("This technique requires a device that supports the 3_0
// vertex shader model", Microsoft Learn, "Efficiently Drawing Multiple
// Instances of Geometry (Direct3D 9)"), and a hardware vs_3_0 pairs only with
// a ps_3_0.
//
// Translated line by line from the disassembly of the game's programs
// (vkd3d-compiler -x d3dbc -b d3d-asm) - SLS2003.vso 996 bytes FNV-1a
// 55A67685, SLS2003.pso 864 bytes FNV-1a C39FE354 (the pixel program of the
// other package, 880 bytes, saturates in other places and is not this one) -
// and their constant tables (CTAB). Every constant is a float4 of its own on
// its register, so that a register means one row and nothing depends on the
// compiler's matrix packing.
//
// Class C: the same math with the model-space constants rebuilt from the
// instance transform; compared against the original's uploads within a
// tolerance ([Route] DrawInstancing=1).
//
// Built with tools/build-shaders.sh (vkd3d-compiler -x hlsl -b d3dbc -p vs_3_0
// -e VS / -p ps_3_0 -e PS) into plugin/src/shaders/SLS2003InstancedBytecode.h.

// Given per draw by the instanced path (rows: register r holds row r):
float4 ViewProj0 : register(c0);  // ModelViewProj with the leader's world taken out: on the instance space
float4 ViewProj1 : register(c1);
float4 ViewProj2 : register(c2);
float4 ViewProj3 : register(c3);
float4 LightDirectionWorld : register(c13);  // LightDirection taken to the instance space (its length free: normalised below)
float4 FogParam : register(c23);  // the engine's own upload
float4 FogColor : register(c24);  // the engine's own upload
float4 ShadowProj0 : register(c28);  // ShadowProj with the leader's world taken out (row 2 is not read, as in the original)
float4 ShadowProj1 : register(c29);
float4 ShadowProj3 : register(c31);
float4 ShadowProjData : register(c32);       // the engine's own upload
float4 ShadowProjTransform : register(c33);  // the engine's own upload
// x: 1 when the leader's c28..c31 were its own world rows (rotation without
// the scale, absolute translation, last row (0, 0, 0, scale) - 0x00B45560,
// measured 2026-11-12), then each copy computes its own; 0 when they were
// another affine map of the packed world (ShadowProj0..3 above, derived).
float4 ShadowMode : register(c34);

struct VSIn {
	float4 position : POSITION0;
	float4 tangent : TANGENT0;
	float4 binormal : BINORMAL0;
	float4 normal : NORMAL0;
	float4 uv : TEXCOORD0;
	float4 color : COLOR0;
	// The instance stream: the world transform's rows as the engine packs
	// them (0x00761AE0: rotation times scale, translation minus the origin
	// 0x00B3F92C), row r = (sR[r][0], sR[r][1], sR[r][2], t[r] - o[r]).
	float4 world0 : TEXCOORD5;
	float4 world1 : TEXCOORD6;
	float4 world2 : TEXCOORD7;
	float4 extra : TEXCOORD4;  // the absolute translation and the scale
};

struct VSOut {
	float4 position : POSITION;
	float4 diffuse : COLOR0;
	float4 fog : COLOR1;
	float4 uv : TEXCOORD0;
	float4 light : TEXCOORD1;
	float4 shadow : TEXCOORD7;
};

VSOut VS(VSIn v) {
	VSOut o;
	const float4 p = float4(v.position.xyz, 1.0f);
	const float4 wp = float4(dot(v.world0, p), dot(v.world1, p), dot(v.world2, p), 1.0f);
	// dp4 oPos, c0..c3, v0 in the original.
	const float4 clip = float4(dot(ViewProj0, wp), dot(ViewProj1, wp), dot(ViewProj2, wp), dot(ViewProj3, wp));
	o.position = clip;
	// dp3 of the tangent frame with c13, normalised (oT1). Here the frame is
	// taken to the instance space by the instance rotation and dotted with the
	// direction in that space: for a rotation with a uniform scale every
	// component differs from the original's by one factor, which the
	// normalisation removes.
	const float3 t = float3(dot(v.world0.xyz, v.tangent.xyz), dot(v.world1.xyz, v.tangent.xyz), dot(v.world2.xyz, v.tangent.xyz));
	const float3 b = float3(dot(v.world0.xyz, v.binormal.xyz), dot(v.world1.xyz, v.binormal.xyz), dot(v.world2.xyz, v.binormal.xyz));
	const float3 n = float3(dot(v.world0.xyz, v.normal.xyz), dot(v.world1.xyz, v.normal.xyz), dot(v.world2.xyz, v.normal.xyz));
	const float3 l = LightDirectionWorld.xyz;
	o.light = float4(normalize(float3(dot(t, l), dot(b, l), dot(n, l))), 0.0f);
	// oT7: the projected map, rows 0, 1 and 3 of c28..c31 on the model
	// position - the copy's own world rows, or the derived rows on the
	// instance space.
	float r1x;
	float r1y;
	float r1w;
	if (ShadowMode.x > 0.5f) {
		const float scale = v.extra.w;
		r1x = dot(v.world0.xyz, p.xyz) / scale + v.extra.x;
		r1y = dot(v.world1.xyz, p.xyz) / scale + v.extra.y;
		r1w = scale;
	} else {
		r1x = dot(ShadowProj0, wp);
		r1y = dot(ShadowProj1, wp);
		r1w = dot(ShadowProj3, wp);
	}
	const float2 r2 = r1w * ShadowProjTransform.xy + float2(r1x, r1y);
	o.shadow.xy = r2 * (1.0f / (r1w * ShadowProjTransform.w));
	const float invData = 1.0f / ShadowProjData.w;
	o.shadow.z = (r1x - ShadowProjData.x) * invData;
	o.shadow.w = 1.0f - (r1y - ShadowProjData.y) * invData;
	// oD1: the fog colour, and the fog by the length of the clip position's xyz.
	const float fog = saturate((FogParam.x - length(clip.xyz)) * (1.0f / FogParam.y));
	o.fog = float4(FogColor.xyz, 1.0f - fog);
	o.uv = float4(v.uv.xy, 0.0f, 0.0f);
	o.diffuse = v.color;
	return o;
}

float4 AmbientColor : register(c1);   // the engine's own upload
float4 PSLightColor : register(c2);   // the engine's own upload
float4 Toggles : register(c7);        // the engine's own upload
sampler2D BaseMap : register(s0);
sampler2D NormalMap : register(s1);
sampler2D ShadowMap : register(s6);
sampler2D ShadowMaskMap : register(s7);

struct PSIn {
	float4 diffuse : COLOR0;
	float4 fog : COLOR1;
	float4 uv : TEXCOORD0;
	centroid float4 light : TEXCOORD1;
	float4 shadow : TEXCOORD7;
};

// SLS2003.pso line by line (its constant c0 = (-0.5, -1, 1, 0)).
float4 PS(PSIn i) : COLOR0 {
	const float4 normalSample = tex2D(NormalMap, i.uv.xy);
	const float3 normal = normalize((normalSample.xyz - 0.5f) * 2.0f);
	const float3 lit = saturate(dot(normal, i.light.xyz) * PSLightColor.xyz);
	const float4 mask = tex2D(ShadowMaskMap, float2(i.shadow.z, i.shadow.w));
	const float4 shadow = tex2D(ShadowMap, i.shadow.xy);
	const float3 shade = mask.x * (shadow.xyz - 1.0f) + 1.0f;
	const float3 light = saturate(lit * shade + AmbientColor.xyz);
	const float4 base = tex2D(BaseMap, i.uv.xy);
	const float3 vertexLit = base.xyz * i.diffuse.xyz;
	const float3 albedo = -Toggles.x >= 0.0f ? base.xyz : vertexLit;
	const float3 fogged = light * albedo + i.fog.w * (i.fog.xyz - light * albedo);
	const float3 color = -Toggles.y >= 0.0f ? light * albedo : fogged;
	return float4(color, base.w * AmbientColor.w);
}
