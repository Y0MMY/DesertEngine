Shader "SkyTransmittanceLut"
{
    Compute
    {
        // The transmittance LUT of the physical atmosphere (Hillaire 2020): for every (view height,
        // view zenith angle) pair, how much of each wavelength survives the trip from that point to the
        // top of the atmosphere. 256x64 RGBA16F, re-dispatched by SkyboxRenderer ONLY when the
        // atmosphere parameter block changes — the texel is a function of the medium alone, not of the
        // camera, the sun or time.
        //
        // Everything of substance lives in Common/SkyMedium.glslh, which is compiled as C++ by the
        // SkyMedium test suite: this stage is the UV -> (r, mu) mapping, one exp(-opticalDepth) march,
        // and an imageStore.

        #include <Common/Atmosphere.glslh>
        #include <Common/SkyMedium.glslh>

        layout(binding = 0, rgba16f) restrict writeonly uniform image2D u_TransmittanceLut;

        // The SAME sky parameter buffer every other sky consumer reads (Graphic::kSkyPayloadBinding);
        // the medium block is vec4s 7-12. The binding is bound explicitly from C++
        // (ComputePipeline::SetStorageBuffer), so this number and that constant must stay equal.
        ReadBuffer(1) SkyBuffer
        {
            vec4 u_SkyPacked[SKY_PACKED_VEC4_COUNT];
        };

        // The step budget is SKY_TRANSMITTANCE_SAMPLE_COUNT, declared in Common/SkyMedium.glslh next to
        // the march it drives: the CPU evaluation that reddens the directional light
        // (Graphic::SunTransmittanceAtGround) marches the same text and must march the same number of
        // steps, or the light and this LUT answer one question twice.

        LocalSize(8, 8, 1);
        void main()
        {
            ivec2 size  = imageSize(u_TransmittanceLut);
            ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
            if (coord.x >= size.x || coord.y >= size.y)
                return;

            SkyPacked s;
            for (int i = 0; i < SKY_PACKED_VEC4_COUNT; ++i)
                s.v[i] = u_SkyPacked[i];

            SkyAtmParams atm = SkyMakeAtmParams(UnpackMediumRayleigh(s), UnpackMediumMie(s),
                                                UnpackMediumMieAbsorption(s), UnpackMediumOzone(s),
                                                UnpackMediumGround(s), UnpackMediumTentPlanet(s));

            // Bruneton's mapping wants the RAW uv (no texel-centre remap): its own distance
            // parameterisation already puts the domain ends on the edge texels.
            vec2 uv = (vec2(coord) + 0.5f) / vec2(size);

            SkyTransmittanceLutCoord c =
                 SkyTransmittanceLutParamsFromUv(atm.BottomRadiusKm, atm.TopRadiusKm, uv);

            vec3 transmittance =
                 SkyTransmittanceToTop(atm, c.ViewHeightKm, c.ViewZenithCos, SKY_TRANSMITTANCE_SAMPLE_COUNT);

            imageStore(u_TransmittanceLut, coord, vec4(transmittance, 1.0f));
        }
    }
}
