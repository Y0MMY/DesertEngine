Shader "CloudShapeNoise"
{
    Compute
    {
        // Fills the 128^3 RGBA8F cloud SHAPE volume. One invocation per texel, one dispatch, once per
        // distinct shape seed — see Graphic::CloudNoiseVolumes.
        //
        // R = Perlin-Worley base shape, G/B/A = Worley FBM at rising frequencies (the erosion octaves).
        // The maths is in Common/CloudNoise.glslh, which the CPU unit tests compile from the same text —
        // there is no second copy of these formulas to keep in step.

        #include <Common/CloudNoise.glslh>

        // rgba8 (not rgba8f): the storage-image format qualifier for an RGBA8 UNORM image is `rgba8`, and
        // it must match the VkFormat the engine creates for Core::Formats::ImageFormat::RGBA8F. imageStore
        // clamps and quantises the [0,1] values written below.
        layout(binding = 0, rgba8) restrict writeonly uniform image3D u_ShapeNoise;

        PushConstant NoisePush
        {
            uint u_Seed;
        };

        LocalSize(8, 8, 8);
        void main()
        {
            ivec3 size  = imageSize(u_ShapeNoise);
            ivec3 coord = ivec3(gl_GlobalInvocationID);

            // The volume edge is a multiple of the work-group edge (asserted in CloudNoiseRules.hpp), so
            // this cannot fire for the sizes we dispatch; it is here so the shader stays correct if a
            // future volume size is not a multiple of 8, rather than writing outside the image.
            if (coord.x >= size.x || coord.y >= size.y || coord.z >= size.z)
                return;

            // Texel CENTRES, so the sampled field is symmetric about the tile and the wrap-around
            // neighbours of texel 0 and texel N-1 are the same distance apart as any interior pair.
            // Sampling at texel corners instead puts a half-texel shift into the seam.
            vec3 p = (vec3(coord) + vec3(0.5f, 0.5f, 0.5f)) / vec3(size);

            imageStore(u_ShapeNoise, coord, CloudShapeTexel(p, u_Seed));
        }
    }
}
