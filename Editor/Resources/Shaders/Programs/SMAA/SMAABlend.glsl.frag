#version 450 core

// SMAA pass 3: neighborhood blending. Reads the tonemapped color + the blend-weights texture and
// produces the final anti-aliased color.
#include "Common/SMAA.glslh"

layout(location = 0) in  vec2 v_TexCoord;
layout(binding  = 2) uniform sampler2D u_ColorTex;
layout(binding  = 3) uniform sampler2D u_BlendTex;
layout(binding  = 4) uniform sampler2D u_EdgesTex; // DIAGNOSTIC only
layout(binding  = 5) uniform sampler2D u_AreaTex;  // DIAGNOSTIC only
layout(location = 0) out vec4 oColor;

// DIAGNOSTIC: 1 = grayscale blend-weight magnitude (x4); 0 = normal SMAA output.
#define SMAA_DEBUG_VIEW 0

void main()
{
    vec2 ts      = vec2( textureSize( u_ColorTex, 0 ) );
    vec4 metrics = vec4( 1.0 / ts, ts );

#if SMAA_DEBUG_VIEW
    vec4  w    = texture( u_BlendTex, v_TexCoord );          // blend weights
    float wmag = ( w.r + w.g + w.b + w.a );
    oColor = vec4( vec3( min( wmag * 4.0, 1.0 ) ), 1.0 );
    return;
#else
    oColor = SMAANeighborhoodBlending( u_ColorTex, u_BlendTex, v_TexCoord, metrics );
#endif
}
