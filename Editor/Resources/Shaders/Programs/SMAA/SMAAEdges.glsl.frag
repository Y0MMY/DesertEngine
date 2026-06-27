#version 450 core

// SMAA pass 1: luma edge detection. Reads the tonemapped LDR color; writes 2-channel edges into .rg.
#include "Common/SMAA.glslh"

layout(location = 0) in  vec2 v_TexCoord;
layout(binding  = 2) uniform sampler2D u_ColorTex;
layout(location = 0) out vec4 oColor;

void main()
{
    vec2 edges = SMAALumaEdgeDetection( u_ColorTex, v_TexCoord );
    oColor = vec4( edges, 0.0, 0.0 );
}
