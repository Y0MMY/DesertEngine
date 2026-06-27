#version 450 core

// Fully procedural physical sky: single-scattering Rayleigh + Mie atmosphere raymarched per pixel
// (no HDR texture — generated entirely on the GPU from the view ray and the sun direction). The sun
// direction is driven by the scene's directional light; output is LINEAR HDR (the post-process
// tonemap pass applies exposure/gamma downstream, exactly like the old skybox did).
//
// The atmosphere model lives in Common/Atmosphere.glslh and is shared with the IBL bake
// (Compute/BakeProceduralSky.glsl.comp) so the visible sky and the light it casts are identical.

#include "Common/Atmosphere.glslh"
#include "Common/Clouds.glslh"

layout(location = 0) in vec3 v_RayDir;
layout(location = 0) out vec4 oColor;

layout(binding = 1) uniform SkyUB
{
    vec4 u_SunDirection; // xyz = direction TOWARD the sun (normalized), w = sun intensity
    vec4 u_SkyParams;    // x = sun angular radius; y = clouds enabled (>0.5); z = coverage; w = density
    vec4 u_CloudParams;  // x = layer base altitude; y = thickness; z = time (s); w = wind speed
    vec4 u_CameraPos;    // xyz = world camera position
};

void main()
{
    vec3  dir    = normalize(v_RayDir);
    vec3  sunDir = normalize(u_SunDirection.xyz);
    float sunI   = u_SunDirection.w;

    vec3 color = EvaluateSky(dir, sunDir, sunI, u_SkyParams.x);

    // Engine-generated volumetric clouds (composited over the atmosphere).
    if (u_SkyParams.y > 0.5)
    {
        color = RenderClouds(color, u_CameraPos.xyz, dir, sunDir, sunI,
                             u_SkyParams.z, u_SkyParams.w,
                             u_CloudParams.x, u_CloudParams.y, u_CloudParams.z, u_CloudParams.w);
    }

    oColor = vec4(color, 1.0);
}
