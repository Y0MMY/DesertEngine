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
    // --- artistic palette/scalars (from ECS::SkyboxComponent) ---
    vec4 u_ZenithColor;  // rgb, w = skyBrightness
    vec4 u_HorizonColor; // rgb, w = horizonFalloff
    vec4 u_SunColor;     // rgb, w = sunGlow
    vec4 u_SunsetColor;  // rgb, w = sunsetIntensity
    vec4 u_GroundColor;  // rgb, w = starIntensity
    vec4 u_NightColor;   // rgb (night sky tint)
    vec4 u_WindDir;      // xy = shared scene wind direction (normalized, ground XZ), zw unused
};

void main()
{
    vec3  dir    = normalize(v_RayDir);
    vec3  sunDir = normalize(u_SunDirection.xyz);
    float sunI   = u_SunDirection.w;

    SkyConfig cfg;
    cfg.zenith          = u_ZenithColor.rgb;
    cfg.horizon         = u_HorizonColor.rgb;
    cfg.sunColor        = u_SunColor.rgb;
    cfg.sunsetColor     = u_SunsetColor.rgb;
    cfg.ground          = u_GroundColor.rgb;
    cfg.night           = u_NightColor.rgb;
    cfg.skyBrightness   = u_ZenithColor.w;
    cfg.horizonFalloff  = u_HorizonColor.w;
    cfg.sunGlow         = u_SunColor.w;
    cfg.sunsetIntensity = u_SunsetColor.w;
    cfg.starIntensity   = u_GroundColor.w;

    vec3 color = EvaluateSky(dir, sunDir, sunI, u_SkyParams.x, cfg);

    // Procedural flat-layer clouds (composited over the sky). CloudParams: x=tiling, y=brightness, z=time,
    // w=wind. SkyParams: z=coverage, w=density.
    if (u_SkyParams.y > 0.5)
    {
        color = RenderClouds(color, dir, sunDir, sunI,
                             u_SkyParams.z, u_SkyParams.w,
                             u_CloudParams.x, u_CloudParams.y, u_CloudParams.z, u_CloudParams.w,
                             u_WindDir.xy);
    }

    // (Dithering is done at the FINAL 8-bit output in the composite/tonemap pass — doing it here in linear
    // HDR is lost through Reinhard+gamma. See SceneComposite.glsl.frag.)
    oColor = vec4(color, 1.0);
}
