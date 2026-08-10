Shader "CloudDetailNoise"
{
    Compute
    {
        // Fills the 32^3 RGBA8F cloud DETAIL volume — the high-frequency field the raymarch erodes the
        // base shape with. Four Worley FBMs at rising frequencies: R/G are the low pair (wispy erosion),
        // B/A the high pair (billowy erosion).
        //
        // Same generator, same file, same tiling guarantee as the shape volume; only the channel
        // frequencies and the seed differ. See Common/CloudNoise.glslh.

        #include <Common/CloudNoise.glslh>

        layout(binding = 0, rgba8) restrict writeonly uniform image3D u_DetailNoise;

        PushConstant NoisePush
        {
            uint u_Seed;
        };

        LocalSize(8, 8, 8);
        void main()
        {
            ivec3 size  = imageSize(u_DetailNoise);
            ivec3 coord = ivec3(gl_GlobalInvocationID);

            if (coord.x >= size.x || coord.y >= size.y || coord.z >= size.z)
                return;

            vec3 p = (vec3(coord) + vec3(0.5f, 0.5f, 0.5f)) / vec3(size);

            imageStore(u_DetailNoise, coord, CloudDetailTexel(p, u_Seed));
        }
    }
}
