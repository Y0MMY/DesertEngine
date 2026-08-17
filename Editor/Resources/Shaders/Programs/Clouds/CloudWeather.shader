Shader "CloudWeather"
{
    Compute
    {
        // Stage S1 of the volumetric clouds: the WEATHER MAP and the PROFILE MAP.
        //
        // Two 512x512 RGBA8 tiles that say, for every place on the ground plane, how much sky is filled
        // there, what kind of cloud it is, how wet it is, how dense it gets, and which vertical SLAB of
        // the layer its cloud occupies. The raymarch reads them once per sample and never re-derives any
        // of it — this is the field that turns a noise volume into weather.
        //
        //   Weather map
        //   R  coverage        after the domain warp, the FBM, the Coverage cut and Coverage Contrast
        //   G  cloud type      Cloud Type, varied across the map by Cloud Type Variance
        //   B  wetness         Wetness, varied the same way; darkens bases and feeds precipitation
        //   A  density scale   a low-frequency field the raymarch shapes with Density Scale Power
        //
        //   Profile map (Nubis3 p. 19's Min Height / Max Height NDFs)
        //   R  min height      where this cell's cloud BASE sits inside the layer
        //   G  max height      where its ceiling sits
        //   B  A               unwritten. RGBA8 is the only 8-bit format the engine creates and the
        //                      field needs two channels; the pair costs a megabyte of a map that is
        //                      baked once per weather change, not per frame.
        //
        // ONE dispatch writes both. The warp below is the expensive part of this shader and both fields
        // are functions of the same warped lookup, so a second pass would warp the domain twice to
        // produce fields that have to agree about where a cloud is.
        //
        // It is regenerated only when a field of the Weather group changes (the renderer compares them),
        // not per frame: nothing in it depends on the camera or on time. The SCROLL is applied at sample
        // time in the raymarch instead, which is why the map has to TILE — and it does, structurally:
        // every function in Common/CloudNoise.glslh has period 1 in every axis, and a periodic warp of a
        // periodic field is still periodic.

        #include <Common/CloudNoise.glslh>
        #include <Common/CloudGeometry.glslh>
        #include <Common/CloudParams.glslh>
        #include <Common/CloudProfile.glslh>

        // rgba8, matching the VkFormat the engine creates for ImageFormat::RGBA8F. imageStore clamps and
        // quantises the [0,1] values written below.
        //
        // VOLUMES, one slice per cloud layer: a deck and a high sheet have different coverage, seed, type
        // and tile size, so they cannot share a map. One dispatch of depth CLOUD_MAX_LAYERS fills them
        // all — the warp is per texel and per layer either way, and a second dispatch would only be a
        // second place for the layer index to come from.
        layout(binding = 0, rgba8) restrict writeonly uniform image3D u_WeatherOut;
        layout(binding = 1, rgba8) restrict writeonly uniform image3D u_ProfileOut;

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
            ivec3 size  = imageSize(u_WeatherOut);
            ivec3 coord = ivec3(gl_GlobalInvocationID.xyz);
            if (coord.x >= size.x || coord.y >= size.y || coord.z >= size.z)
                return;

            // THE SLICE IS THE LAYER. Every u_ name below reads the copy CloudSelectLayer makes, so
            // this one line is the whole of what makes one dispatch bake two different cloudscapes.
            CloudSelectLayer(min(coord.z, u_LayerCount - 1));

            // Texel CENTRES, so the wrap-around neighbours of texel 0 and texel N-1 sit the same
            // distance apart as any interior pair. Sampling at corners puts a half-texel shift into the
            // seam, and this map is tiled across a hundred kilometres of sky.
            vec2 uv   = (vec2(float(coord.x), float(coord.y)) + vec2(0.5f, 0.5f)) /
                        vec2(float(size.x), float(size.y));
            uint seed = uint(u_WeatherSeed);

            // Domain warp. Circular blobs are what an unwarped FBM threshold always produces; pushing
            // the lookup around by a second, lower-frequency noise is what turns them into fronts.
            vec2 warp = vec2(CloudPerlin(vec3(uv.x, uv.y, 0.17f), 3, seed + 8191u),
                             CloudPerlin(vec3(uv.x, uv.y, 0.83f), 3, seed + 9209u));
            vec2 p    = uv + warp * (u_WeatherWarpStrength * 0.35f);

            // CLOUD_WEATHER_BASE_PERIOD, not a literal. The dominant coverage cell is
            // WeatherTileSize / that period, and what makes a ground observer's sky believable is how that
            // cell compares with the disc of map the sky above thirty degrees actually covers — see
            // CloudAutoWeatherTileSize in Common/CloudGeometry.glslh, which is the same relation solved
            // for the tile and is what the component's default and every preset are now authored from.
            // Raising the period here without moving those would put a different cell size on the same
            // sky, which is why the number lives in one place.
            float field = WeatherFbm(p, u_WeatherOctaves, int(CLOUD_WEATHER_BASE_PERIOD), seed);

            // Coverage cuts the field from above: Coverage = 0 removes everything, Coverage = 1 keeps
            // the whole range. Contrast then shapes what survives — high gives hard-edged islands, low a
            // soft blanket.
            float coverage = CloudRemapRange(field, 1.0f - u_Coverage, 1.0f, 0.0f, 1.0f);
            coverage       = pow(coverage, u_CoverageContrast);

            // Type and wetness vary across the map around their authored values. Variance of 0 gives one
            // uniform type over the whole sky, which is exactly what a stratus deck is.
            //
            // The SAME base period as the coverage field, for the reason the type describes the same cell
            // the coverage does: a cell whose fill and whose form came from two different scales is two
            // clouds. Cloud Type Variance — a slider whose entire purpose is putting different forms next
            // to each other — can only do that when a shelf and a tower fit side by side in the sky the
            // camera can see, which is what tying the period to the layer's altitude buys.
            float typeNoise = CloudPerlin(vec3(p.x, p.y, 0.41f), int(CLOUD_WEATHER_BASE_PERIOD), seed + 5501u) * 1.5f + 0.5f;
            float type      = CloudProfileTypeAt(u_CloudType, typeNoise, u_CloudTypeVariance);

            float wetNoise = CloudPerlin(vec3(p.x, p.y, 0.59f), 3, seed + 6607u) * 1.5f + 0.5f;
            float wetness  = clamp(u_Wetness * (0.5f + clamp(wetNoise, 0.0f, 1.0f)), 0.0f, 1.0f);

            // The density-scale field is deliberately independent of coverage: a sky can be evenly
            // covered and still have a few much denser cells in it.
            //
            // The FULL [0, 1] range. The raymarch applies this channel LINEARLY and keeps Density Scale
            // Power inside the sharpening lerp only (the Nubis3 p. 118 form — see the comment at
            // CloudDensityProcedural.glslh's density-scale site). The [0.5, 1] compression that used to
            // be written here was a workaround for the days when the raymarch multiplied by scale^4,
            // which arrived as 0.06 for a mid-range cell; with the linear form fixed, the compression
            // only halved the per-cell density variety this channel exists to provide (the deck's third
            // NVDF channel, p. 85) and pinned the sharpen exponent near its 0.6 end.
            float densityScale  = clamp(CloudPerlin(vec3(p.x, p.y, 0.73f), 4, seed + 7717u) * 1.5f + 0.5f,
                                        0.0f, 1.0f);

            imageStore(u_WeatherOut, coord, vec4(coverage, type, wetness, densityScale));

            // The per-cell vertical band (Nubis3 p. 19's Min Height / Max Height NDFs). The construction
            // itself lives in Common/CloudProfile.glslh, where the raymarch's half of the arithmetic is
            // and where a test can compile it.
            //
            // Period 8, seeded away from every other field here, so a cell's altitude is independent of
            // whether there is a cloud there at all: coverage decides WHERE, this decides HOW HIGH.
            float centreNoise = CloudPerlin(vec3(p.x, p.y, 0.29f), int(CLOUD_WEATHER_BASE_PERIOD), seed + 4409u) * 1.6f + 0.5f;
            float widthNoise  = CloudPerlin(vec3(p.x, p.y, 0.67f), int(CLOUD_WEATHER_BASE_PERIOD), seed + 3313u) * 1.6f + 0.5f;

            vec2 heightNdf = CloudProfileHeightNdf(centreNoise, widthNoise);

            imageStore(u_ProfileOut, coord, vec4(heightNdf.x, heightNdf.y, 0.0f, 0.0f));
        }
    }
}
