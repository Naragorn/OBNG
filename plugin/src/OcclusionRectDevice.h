#pragma once

// The device state for drawing a bound's screen rectangle into an occlusion
// query, shared by the occlusion probe (OcclusionProbe.cpp) and the culler
// (OcclusionCull.cpp): fixed function, pre-transformed vertices, the depth
// test on without a depth write, nothing written to any colour target. The
// caller brackets it with a state block (D3DSBT_ALL) captured before and
// applied after, so the engine's own state cache stays right.

#include <d3d9.h>

namespace obng::plugin::occlusion_device {

inline void SetTestState(IDirect3DDevice9* device) {
	device->SetVertexShader(nullptr);
	device->SetPixelShader(nullptr);
	device->SetFVF(D3DFVF_XYZRHW);
	device->SetTexture(0, nullptr);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	const struct {
		D3DRENDERSTATETYPE state;
		DWORD value;
	} states[] = {
	    {D3DRS_ZENABLE, D3DZB_TRUE},        {D3DRS_ZWRITEENABLE, FALSE},      {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},
	    {D3DRS_COLORWRITEENABLE, 0},        {D3DRS_COLORWRITEENABLE1, 0},     {D3DRS_COLORWRITEENABLE2, 0},
	    {D3DRS_COLORWRITEENABLE3, 0},       {D3DRS_ALPHATESTENABLE, FALSE},   {D3DRS_ALPHABLENDENABLE, FALSE},
	    {D3DRS_CULLMODE, D3DCULL_NONE},     {D3DRS_STENCILENABLE, FALSE},     {D3DRS_FOGENABLE, FALSE},
	    {D3DRS_SCISSORTESTENABLE, FALSE},   {D3DRS_CLIPPLANEENABLE, 0},       {D3DRS_FILLMODE, D3DFILL_SOLID},
	    {D3DRS_DEPTHBIAS, 0},               {D3DRS_SLOPESCALEDEPTHBIAS, 0},   {D3DRS_LIGHTING, FALSE},
	};
	for (const auto& s : states) {
		device->SetRenderState(s.state, s.value);
	}
}

}  // namespace obng::plugin::occlusion_device
