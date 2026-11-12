// The instanced counterpart of ShadowLightShader's pass with SLS2003.vso and
// SLS2003.pso (knowledge/draw-instancing.md): one draw for the copies of a mesh
// in one pass, the per-draw constants the engine uploads now taken from a
// second vertex stream (the world transform, the alpha) or derived here from
// constants given once per pass.
//
// Shader model 3 throughout: D3D9 fetches a stream by instance only for a
// vs_3_0 program ("This technique requires a device that supports the 3_0
// vertex shader model", Microsoft Learn, "Efficiently Drawing Multiple
// Instances of Geometry (Direct3D 9)"), and a hardware vs_3_0 pairs only with
// a ps_3_0.
//
// Translated from the disassembly of the game's programs (vkd3d-compiler
// -x d3dbc -b d3d-asm) and their constant tables (CTAB): SLS2003.vso -
// ModelViewProj c0..c3, LightDirection c13, FogParam c23, FogColor c24,
// ShadowProj c28..c31, ShadowProjData c32, ShadowProjTransform c33;
// SLS2003.pso - AmbientColor c1, PSLightColor c2, Toggles c7, BaseMap s0,
// NormalMap s1, ShadowMap s6, ShadowMaskMap s7. Class C: the same math in
// another order (the model-space constants rebuilt from the world transform),
// compared against the original within a tolerance.
//
// Built with: vkd3d-compiler -x hlsl -b d3dbc -p vs_3_0 -e VS ... / -p ps_3_0 -e PS ...

// Given once per pass (the instanced path uploads these):
float4x4 ViewProj : register(c0);        // the view-projection rows: ModelViewProj with the world left out
float4 LightDirectionWorld : register(c13);  // the light direction in (camera-relative) world space
float4 FogParam : register(c23);
float4 FogColor : register(c24);
float4x4 ShadowProjWorld : register(c28);  // ShadowProj with the world left out: the light's matrix on world positions (+ the origin offset)
float4 ShadowProjData : register(c32);
float4 ShadowProjTransform : register(c33);
float4 ShadowOriginOffset : register(c34);  // 0x00B3F92C, added to the translation before ShadowProj (0x007C9230 B35)

struct VSIn {
	float4 position : POSITION0;
	float4 tangent : TANGENT0;
	float4 binormal : BINORMAL0;
	float4 normal : NORMAL0;
	float4 uv : TEXCOORD0;
	float4 color : COLOR0;
	// The instance stream: the world transform's rows (rotation with scale | camera-relative translation) and the alpha.
	float4 world0 : TEXCOORD5;
	float4 world1 : TEXCOORD6;
	float4 world2 : TEXCOORD7;
	float4 instance : TEXCOORD4;  // x: alpha (AmbientColor.w of the draw), yzw unused
};

struct VSOut {
	float4 position : POSITION;
	float4 diffuse : COLOR0;
	float4 fog : COLOR1;
	float4 uv : TEXCOORD0;
	float4 light : TEXCOORD1;
	float4 shadow : TEXCOORD7;
	float4 instance : TEXCOORD2;
};

VSOut VS(VSIn v) {
	VSOut o;
	const float3x4 world = float3x4(v.world0, v.world1, v.world2);
	const float3 worldPos = mul(world, v.position);
	const float4 wp = float4(worldPos, 1.0f);
	// ModelViewProj x position, as the original's dp4 c0..c3.
	const float4 clip = mul(ViewProj, wp);
	o.position = clip;
	// The light direction in tangent space: the original dots the model-space direction with the tangent frame;
	// here the frame is taken to world space (the same rotation the engine's inverse undoes) - equal after the nrm.
	const float3x3 rotation = (float3x3)world;
	const float3 t = mul(rotation, v.tangent.xyz);
	const float3 b = mul(rotation, v.binormal.xyz);
	const float3 n = mul(rotation, v.normal.xyz);
	const float3 l = LightDirectionWorld.xyz;
	o.light = float4(normalize(float3(dot(t, l), dot(b, l), dot(n, l))), 0.0f);
	// The projected map: ShadowProj on the model position = the light's matrix on the world position plus the offset.
	const float4 sp = float4(worldPos + ShadowOriginOffset.xyz, 1.0f);
	const float r1x = dot(ShadowProjWorld[0], sp);
	const float r1y = dot(ShadowProjWorld[1], sp);
	const float r1w = dot(ShadowProjWorld[3], sp);
	const float2 r2 = r1w * ShadowProjTransform.xy + float2(r1x, r1y);
	const float invDepth = 1.0f / (r1w * ShadowProjTransform.w);
	const float invData = 1.0f / ShadowProjData.w;
	o.shadow.xy = invDepth * r2;
	o.shadow.z = (r1x - ShadowProjData.x) * invData;
	o.shadow.w = 1.0f - (r1y - ShadowProjData.y) * invData;
	// The fog by the length of the clip position's xyz, as the original.
	const float fog = saturate((FogParam.x - length(clip.xyz)) / FogParam.y);
	o.fog = float4(FogColor.xyz, 1.0f - fog);
	o.uv = float4(v.uv.xy, 0.0f, 0.0f);
	o.diffuse = v.color;
	o.instance = v.instance;
	return o;
}

float4 AmbientColor : register(c1);   // xyz used; w (the alpha) comes per instance
float4 PSLightColor : register(c2);
float4 Toggles : register(c7);
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
	float4 instance : TEXCOORD2;
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
	return float4(color, base.w * i.instance.x);
}
