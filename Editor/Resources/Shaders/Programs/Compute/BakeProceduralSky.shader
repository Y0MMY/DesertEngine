Shader "BakeProceduralSky"
{
    Compute
    {
        // Bakes the procedural atmosphere into an equirect HDR panorama (Image2D, RGBA32F). The result is fed
        // into the existing IBL pipeline (PanoramaToCubemap -> radiance, DiffuseIrradiance, PrefilterEnvMap)
        // so the procedural sky lights the scene exactly like an HDR environment would. The atmosphere model
        // is the SAME one used by the screen sky pass (Common/Atmosphere.glslh).
        //
        // Equirect mapping matches PanoramaToCubemap.glsl.comp's sampling:
        //   phi = atan(dir.z, dir.x);  theta = acos(dir.y);  uv = (phi/2PI + 0.5, theta/PI)
        // so here we invert it: uv -> (phi, theta) -> direction.

        #include <Common/Atmosphere.glslh>

        layout(binding = 0, rgba32f) restrict writeonly uniform image2D outputPanorama;

        layout(push_constant) uniform PushConstants
        {
            vec4 u_SunDirection; // xyz = direction TOWARD the sun (normalized), w = sun intensity
            vec4 u_SkyParams;    // x = sun angular radius; y = skyBrightness; z = horizonFalloff; w = sunGlow
            vec4 u_Zenith;       // rgb, w = sunsetIntensity
            vec4 u_Horizon;      // rgb, w = starIntensity
            vec4 u_SunColor;     // rgb
            vec4 u_SunsetColor;  // rgb
            vec4 u_Ground;       // rgb
            vec4 u_Night;        // rgb
        };

        layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
        void main()
        {
            ivec2 size  = imageSize(outputPanorama);
            ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
            if (coord.x >= size.x || coord.y >= size.y)
                return;

            vec2  uv    = (vec2(coord) + 0.5) / vec2(size);
            float phi   = (uv.x - 0.5) * 2.0 * ATM_PI;
            float theta = uv.y * ATM_PI;
            float st    = sin(theta);

            // Direction for this panorama texel (y-up, matching the engine's equirect convention).
            vec3 dir = vec3(st * cos(phi), cos(theta), st * sin(phi));

            // Build the SkyConfig from the editor's sky settings so the baked IBL matches the visible sky exactly.
            SkyConfig cfg;
            cfg.zenith          = u_Zenith.rgb;
            cfg.horizon         = u_Horizon.rgb;
            cfg.sunColor        = u_SunColor.rgb;
            cfg.sunsetColor     = u_SunsetColor.rgb;
            cfg.ground          = u_Ground.rgb;
            cfg.night           = u_Night.rgb;
            cfg.skyBrightness   = u_SkyParams.y;
            cfg.horizonFalloff  = u_SkyParams.z;
            cfg.sunGlow         = u_SkyParams.w;
            cfg.sunsetIntensity = u_Zenith.w;
            cfg.starIntensity   = u_Horizon.w;

            vec3 color = EvaluateSky(dir, normalize(u_SunDirection.xyz), u_SunDirection.w, u_SkyParams.x, cfg);

            imageStore(outputPanorama, coord, vec4(color, 1.0));
        }
    }
}
