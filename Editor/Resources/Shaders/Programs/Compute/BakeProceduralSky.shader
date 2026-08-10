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

        // The SAME sky parameter buffer the screen pass reads — not a second hand-packed mirror of it. The
        // bake used to receive these as a push-constant block, which meant two layouts to keep in step and
        // a silent corruption of everything after the first field that fell out of order.
        //
        // The binding number is bound explicitly from C++ (ComputePipeline::SetStorageBuffer) and must stay
        // equal to Graphic::kSkyPayloadBinding and to the number the graphics sky shader declares.
        ReadBuffer(1) SkyBuffer
        {
            vec4 u_SkyPacked[SKY_PACKED_VEC4_COUNT];
        };

        LocalSize(32, 32, 1);
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

            SkyPacked s;
            for (int i = 0; i < SKY_PACKED_VEC4_COUNT; ++i)
                s.v[i] = u_SkyPacked[i];

            vec3 color = EvaluateSky(dir, UnpackSunDirection(s), UnpackSunIntensity(s),
                                     UnpackSunAngularRadius(s), UnpackSkyConfig(s));

            imageStore(outputPanorama, coord, vec4(color, 1.0));
        }
    }
}
