#version 450 core

// Fully procedural physical sky: single-scattering Rayleigh + Mie atmosphere raymarched per pixel
// (no HDR texture — generated entirely on the GPU from the view ray and the sun direction). The sun
// direction is driven by the scene's directional light; output is LINEAR HDR (the post-process
// tonemap pass applies exposure/gamma downstream, exactly like the old skybox did).
//
// The atmosphere model lives in Common/Atmosphere.glslh and is shared with the IBL bake
// (Compute/BakeProceduralSky.glsl.comp) so the visible sky and the light it casts are identical.

#include "Common/Atmosphere.glslh"

layout(location = 0) in vec3 v_RayDir;
layout(location = 0) out vec4 oColor;

layout(binding = 1) uniform SkyUB
{
    vec4 u_SunDirection; // xyz = direction TOWARD the sun (normalized), w = sun intensity
    vec4 u_SkyParams;    // x = sun angular radius (radians); y,z,w reserved (clouds later)
};

void main()
{
    vec3  dir    = normalize(v_RayDir);
    vec3  sunDir = normalize(u_SunDirection.xyz);
    float sunI   = u_SunDirection.w;

    vec3 color = EvaluateSky(dir, sunDir, sunI, u_SkyParams.x);

    oColor = vec4(color, 1.0);
}
