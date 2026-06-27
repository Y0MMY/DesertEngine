#version 450 core

// SMAA pass 2: blending-weight calculation. Reads the edges texture + the precomputed AreaTex/SearchTex
// LUTs; writes per-direction blend weights (rgba).
#include "Common/SMAA.glslh"

layout(location = 0) in  vec2 v_TexCoord;
layout(binding  = 2) uniform sampler2D u_EdgesTex;
layout(binding  = 3) uniform sampler2D u_AreaTex;
layout(binding  = 4) uniform sampler2D u_SearchTex;
layout(location = 0) out vec4 oColor;

void main()
{
    vec2 ts      = vec2( textureSize( u_EdgesTex, 0 ) );
    vec4 metrics = vec4( 1.0 / ts, ts );
    oColor = SMAABlendingWeightCalculation( u_EdgesTex, u_AreaTex, u_SearchTex, v_TexCoord, metrics );
}
