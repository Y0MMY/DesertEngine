Shader "CloudCurlNoise"
{
    Compute
    {
        // Fills the 128x128 RGBA8F curl map. RGB carry the three components of the curl of a vector
        // potential, encoded into [0,1] by the fixed CLOUD_CURL_ENCODE_SCALE of Common/CloudNoise.glslh;
        // the consumer decodes with CLOUD_CURL_DECODE_SCALE. A curl field is divergence-free by
        // construction, which is what lets it swirl the detail lookup without thinning or piling up the
        // density it warps.
        //
        // 2D, not a volume: the swirl is a horizontal-plane effect, and the vertical churn of the detail
        // lookup is the component's Wind Uplift Speed. A 128^3 curl volume would cost 8 MiB to say
        // roughly the same thing.

        #include <Common/CloudNoise.glslh>

        layout(binding = 0, rgba8) restrict writeonly uniform image2D u_CurlNoise;

        PushConstant NoisePush
        {
            uint u_Seed;
        };

        LocalSize(8, 8, 1);
        void main()
        {
            ivec2 size  = imageSize(u_CurlNoise);
            ivec2 coord = ivec2(gl_GlobalInvocationID.xy);

            if (coord.x >= size.x || coord.y >= size.y)
                return;

            vec2 uv = (vec2(coord) + vec2(0.5f, 0.5f)) / vec2(size);

            imageStore(u_CurlNoise, coord, CloudCurlTexel(uv, u_Seed));
        }
    }
}
