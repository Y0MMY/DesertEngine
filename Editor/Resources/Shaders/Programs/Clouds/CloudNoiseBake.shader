Shader "CloudNoiseBake"
{
    Compute
    {
        // Bakes the cloud noise volume: four periodic octaves of Perlin noise, one per channel, into a
        // 128^3 RGBA8 volume that tiles exactly over the unit cube.
        //
        // WHY BAKE AT ALL. The march evaluates the field once per step and again for every light sample;
        // four octaves of Perlin is thirty-two integer hashes, and at ninety-six steps with four light
        // samples that is tens of thousands of hashes per pixel. Both references solve this the same way
        // and for the same reason — UE's cloud material samples 3D textures, Nubis samples a 128^3
        // four-channel volume (deck p.96) — so the fetch replaces the arithmetic and the arithmetic runs
        // once, here, at load.
        //
        // RUN ONCE, NOT PER FRAME. The volume depends only on the seed, so it is filled when the seed
        // changes and never again. Nothing in it is animated: the wind moves the SAMPLE POSITION, which is
        // what makes the motion free and, more importantly, what keeps it seamless — an animated volume
        // would have to be regenerated every frame and would still pop at the tile boundary.
        //
        // THE PERIODS ARE THE POINT. Each channel tiles over a whole number of lattice cells across the
        // unit cube, so REPEAT sampling is exact and there is no seam anywhere. The four are chosen so
        // that the finest still has eight voxels per cell after its octaves have doubled the frequency —
        // below that, gradient noise quantizes onto the voxel grid and reads as a visible lattice rather
        // than as cloud.
        //
        //   R  Perlin-Worley, period  4   coarse coverage: owns where a cloud IS
        //   G  Perlin-Worley, period  8   coverage roughness: breaks up the island edge
        //   B  inverted Worley, period 12  BILLOWY erosion — the lobes a convective cloud is made of
        //   A  curl-sheared Perlin, p. 16  WISPY erosion — the hooks a moving edge tears into
        //
        // THE CHOICE OF NOISE PER CHANNEL IS THE SHAPE. Gradient noise alone is smooth by construction:
        // its level sets are blobs, and a cloud built from it looks like cotton wool however it is tuned.
        // Perlin-Worley (Schneider 2015) keeps the wind-blown continuity of the gradient field while
        // taking its local relief from a cell structure, which is what reads as cauliflower; inverted
        // Worley on its own is the billow; and a gradient field sheared by a divergence-free curl is the
        // wisp. All three were absent from the first version of this volume, and the clouds it produced
        // were smooth to the point of looking like fog.
        //
        // The octave counts are authored, so the finest channel can be pushed to 16 x 2^5 cells across
        // 128 voxels — four voxels per cell, which is where noise starts to quantize onto the voxel grid.
        // That is why the component clamps them at six.
        //
        // ALL FOUR CHANNELS HAVE A CONSUMER in Common/CloudField.glslh — R and G build the coverage
        // field, B and A are the two ends of the DetailType blend. A channel nobody reads is a quarter of
        // the bandwidth of every sample in the march, paid on every frame forever.

        #include <Common/CloudNoise.glslh>

        // rgba8: the noise is a normalized [0,1] field and eight bits per channel is 1/255 — finer than
        // the erosion threshold can act on, and a quarter of the bandwidth of a half-float volume that
        // would carry precision nothing downstream can use.
        layout(binding = 0, rgba8) restrict writeonly uniform image3D u_CloudNoiseOut;

        PushConstant CloudNoiseBakePush
        {
            // x = coverage seed, y = erosion seed, z = coverage octaves, w = erosion octaves.
            // The two seeds are separate rather than derived from one another so that re-rolling the
            // coverage does not also re-roll the erosion — an artist who likes the billows and wants
            // different islands must be able to say so.
            uvec4 u_NoiseBake;
        };

        LocalSize(8, 8, 8);

        void main()
        {
            ivec3 size  = imageSize(u_CloudNoiseOut);
            ivec3 coord = ivec3(gl_GlobalInvocationID.xyz);
            if (coord.x >= size.x || coord.y >= size.y || coord.z >= size.z)
                return;

            // Voxel CENTRES, not corners. Sampling the corner makes the last voxel of the volume the same
            // lattice value as the first, which is a half-voxel phase error that shows as a faint plane
            // at the tile boundary — exactly the seam the periodicity was for.
            vec3 uvw = (vec3(coord) + vec3(0.5f, 0.5f, 0.5f)) / vec3(size);

            uint coverageSeed = u_NoiseBake.x;
            uint erosionSeed  = u_NoiseBake.y;

            // Clamped rather than trusted: the counts drive a loop, and a scene file carrying a large
            // value would not bake badly, it would hang the device.
            int coverageOctaves = int(clamp(u_NoiseBake.z, 1u, 6u));
            int erosionOctaves  = int(clamp(u_NoiseBake.w, 1u, 6u));

            // Within a pair the two channels are offset in the seed space, so the coarse and the fine
            // octave cannot produce correlated maxima that would read as one octave with a strange
            // amplitude.
            // Shear of the wispy channel, in lattice cells. A third of a cell is enough to hook an edge
            // over without dragging the field so far that its own periodicity stops being visible as
            // continuity — past about half a cell the wisps stop belonging to the cloud they came from.
            const float kCurlStrength = 0.33f;

            float r = CloudPerlinWorley01(uvw *  4.0f,  4.0f, coverageSeed +    0u, coverageOctaves);
            float g = CloudPerlinWorley01(uvw *  8.0f,  8.0f, coverageSeed +  977u, coverageOctaves);
            float b = CloudWorleyFbm     (uvw * 12.0f, 12.0f, erosionSeed  + 1861u, erosionOctaves);
            float a = CloudCurlyPerlin01 (uvw * 16.0f, 16.0f, erosionSeed  + 2749u, erosionOctaves,
                                          kCurlStrength);

            imageStore(u_CloudNoiseOut, coord, vec4(r, g, b, a));
        }
    }
}
