Shader "CloudWeather"
{
    Compute
    {
        // Stage S1 of the volumetric clouds: the WEATHER MAP.
        //
        // A 512x512 RGBA8 tile that says, for every place on the ground plane, how much sky is filled
        // there, what kind of cloud it is, how wet it is and how dense it gets. The raymarch reads it
        // once per sample and never re-derives any of it — this is the field that turns a noise volume
        // into weather.
        //
        //   R  coverage        after the domain warp, the FBM, the Coverage cut and Coverage Contrast
        //   G  cloud type      Cloud Type, varied across the map by Cloud Type Variance
        //   B  wetness         Wetness, varied the same way; darkens bases and feeds precipitation
        //   A  density scale   a low-frequency field the raymarch shapes with Density Scale Power
        //
        // It is regenerated only when a field of the Weather group changes (the renderer compares them),
        // not per frame: nothing in it depends on the camera or on time. The SCROLL is applied at sample
        // time in the raymarch instead, which is why the map has to TILE — and it does, structurally:
        // every function in Common/CloudNoise.glslh has period 1 in every axis, and a periodic warp of a
        // periodic field is still periodic.

        #include <Common/CloudNoise.glslh>
        #include <Common/CloudGeometry.glslh>
        #include <Common/CloudParams.glslh>

        // rgba8, matching the VkFormat the engine creates for ImageFormat::RGBA8F. imageStore clamps and
        // quantises the [0,1] values written below.
        layout(binding = 0, rgba8) restrict writeonly uniform image2D u_WeatherOut;

        LocalSize(8, 8, 1);

        // Fractal Brownian motion over the periodic Perlin lattice, with the octave count the component
        // authors. The period DOUBLES per octave so every octave is periodic over the same tile, and the
        // amplitudes are normalised by their own sum so changing the octave count changes the detail and
        // not the overall level — an octave slider that also darkened the sky would be unusable.
        float WeatherFbm(vec2 uv, int octaves, int basePeriod, uint seed)
        {
            float total  = 0.0f;
            float amp    = 1.0f;
            float norm   = 0.0f;
            int   period = basePeriod;

            for (int i = 0; i < octaves; ++i)
            {
                total += amp * CloudPerlin(vec3(uv.x, uv.y, 0.5f), period, seed + uint(i) * 131u);
                norm += amp;
                amp *= 0.5f;
                period *= 2;
            }

            // Gradient noise reaches about +/-0.7 of its nominal range; the same normalisation
            // Common/CloudNoise.glslh's Perlin FBM uses, for the same reason.
            return clamp((total / max(norm, 1e-6f)) * 1.05f + 0.5f, 0.0f, 1.0f);
        }

        void main()
        {
            ivec2 size  = imageSize(u_WeatherOut);
            ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
            if (coord.x >= size.x || coord.y >= size.y)
                return;

            // Texel CENTRES, so the wrap-around neighbours of texel 0 and texel N-1 sit the same
            // distance apart as any interior pair. Sampling at corners puts a half-texel shift into the
            // seam, and this map is tiled across a hundred kilometres of sky.
            vec2 uv   = (vec2(coord) + vec2(0.5f, 0.5f)) / vec2(size);
            uint seed = uint(u_WeatherSeed);

            // Domain warp. Circular blobs are what an unwarped FBM threshold always produces; pushing
            // the lookup around by a second, lower-frequency noise is what turns them into fronts.
            vec2 warp = vec2(CloudPerlin(vec3(uv.x, uv.y, 0.17f), 3, seed + 8191u),
                             CloudPerlin(vec3(uv.x, uv.y, 0.83f), 3, seed + 9209u));
            vec2 p    = uv + warp * (u_WeatherWarpStrength * 0.35f);

            float field = WeatherFbm(p, u_WeatherOctaves, 2, seed);

            // Coverage cuts the field from above: Coverage = 0 removes everything, Coverage = 1 keeps
            // the whole range. Contrast then shapes what survives — high gives hard-edged islands, low a
            // soft blanket.
            float coverage = CloudRemapRange(field, 1.0f - u_Coverage, 1.0f, 0.0f, 1.0f);
            coverage       = pow(coverage, u_CoverageContrast);

            // Type and wetness vary across the map around their authored values. Variance of 0 gives one
            // uniform type over the whole sky, which is exactly what a stratus deck is.
            float typeNoise = CloudPerlin(vec3(p.x, p.y, 0.41f), 2, seed + 5501u) * 1.5f + 0.5f;
            float type      = clamp(u_CloudType + (clamp(typeNoise, 0.0f, 1.0f) - 0.5f) * u_CloudTypeVariance,
                                    0.0f, 1.0f);

            float wetNoise = CloudPerlin(vec3(p.x, p.y, 0.59f), 3, seed + 6607u) * 1.5f + 0.5f;
            float wetness  = clamp(u_Wetness * (0.5f + clamp(wetNoise, 0.0f, 1.0f)), 0.0f, 1.0f);

            // The density-scale field is deliberately independent of coverage: a sky can be evenly
            // covered and still have a few much denser cells in it.
            float densityScale = clamp(CloudPerlin(vec3(p.x, p.y, 0.73f), 4, seed + 7717u) * 1.5f + 0.5f,
                                       0.0f, 1.0f);

            imageStore(u_WeatherOut, coord, vec4(coverage, type, wetness, densityScale));
        }
    }
}
