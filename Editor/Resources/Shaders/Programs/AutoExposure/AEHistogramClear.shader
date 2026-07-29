Shader "AEHistogramClear"
{
    Compute
    {
        // Auto-exposure: zero the 256-bin luminance histogram before AEHistogram accumulates into it. Run as a
        // GPU pass (256 threads) so there's no host<->device sync on the per-frame histogram buffer.

        LocalSize(256, 1, 1);

        Buffer(1) Histogram
        {
            uint u_Bins[256];
        };

        void main()
        {
            u_Bins[gl_GlobalInvocationID.x] = 0u;
        }
    }
}
