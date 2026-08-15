Shader "BakeProceduralSky"
{
    Compute
    {
        // Bakes the procedural sky into an equirect HDR panorama (Image2D, RGBA32F). The result is fed
        // into the existing IBL pipeline (PanoramaToCubemap -> radiance, DiffuseIrradiance, PrefilterEnvMap)
        // so the procedural sky lights the scene exactly like an HDR environment would. Both sky models
        // are the SAME evaluations the screen pass runs (Common/Atmosphere.glslh for the gradient,
        // Common/SkyScattering.glslh's integrator for the physical atmosphere) — the baked environment
        // and the visible sky cannot drift apart.
        //
        // The physical branch marches the integrator directly per texel (32 samples, the Sky-View
        // budget) instead of sampling the per-view Sky-View LUT: the bake is anchored at a fixed point,
        // not at the camera, and must not change when the camera does — its cadence is driven by the
        // sun alone. Two deliberate divergences from the screen pass, both invisible to the IBL:
        //   * anchored at a fixed 0.2 km — the sky varies imperceptibly across ground-level altitudes,
        //     and anchoring at the camera would demand a rebake on every elevator ride;
        //   * NO analytic sun disc — the disc's energy over a panorama texel is the whole direct sun
        //     illuminance, and the directional light already delivers exactly that to every surface;
        //     baking it too would double-count the sun in the irradiance cube.
        //
        // Equirect mapping matches PanoramaToCubemap.glsl.comp's sampling:
        //   phi = atan(dir.z, dir.x);  theta = acos(dir.y);  uv = (phi/2PI + 0.5, theta/PI)
        // so here we invert it: uv -> (phi, theta) -> direction.

        #include <Common/Atmosphere.glslh>
        #include <Common/SkyMedium.glslh>

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

        // The cached atmosphere LUTs (Graphic::kSkyTransmittanceLutBinding / kSkyMultiScatterLutBinding).
        // SkyboxRenderer dispatches them immediately before a physical bake; on the gradient model the
        // C++ side binds fallbacks and the physical branch below never runs.
        Uniform(2) sampler2D u_TransmittanceLut;
        Uniform(3) sampler2D u_MultiScatterLut;

        // The same read-side mappings the SkyViewLut fill uses: raw uv for the transmittance LUT,
        // texel-centre remap for the multi-scatter LUT — each the exact inverse of its write side.
        vec3 SkySampleSunTransmittanceLut(SkyAtmParams atm, float radiusKm, float sunZenithCos)
        {
            vec2 uv = SkyTransmittanceLutUvFromParams(atm.BottomRadiusKm, atm.TopRadiusKm, radiusKm,
                                                      sunZenithCos);
            return texture(u_TransmittanceLut, uv).rgb;
        }

        vec3 SkySampleMultiScatterLut(SkyAtmParams atm, float radiusKm, float sunZenithCos)
        {
            vec2 unit = SkyMultiScatterUnitFromParams(atm.BottomRadiusKm, atm.TopRadiusKm, radiusKm,
                                                      sunZenithCos);
            vec2 uv   = vec2(SkyUnitToTexelUv(unit.x, 32.0f), SkyUnitToTexelUv(unit.y, 32.0f));
            return texture(u_MultiScatterLut, uv).rgb;
        }

        #define SKY_SCATTERING_SUN_TRANSMITTANCE(atm, radiusKm, sunZenithCos) SkySampleSunTransmittanceLut(atm, radiusKm, sunZenithCos)
        #define SKY_SCATTERING_MULTI_SCATTER(atm, radiusKm, sunZenithCos) SkySampleMultiScatterLut(atm, radiusKm, sunZenithCos)

        #include <Common/SkyScattering.glslh>

        const int   kBakeSampleCount     = 32;   // the Sky-View budget; the bake is off the frame path
        const float kBakeAnchorAltitudeKm = 0.2f; // the fixed viewpoint (see the header)

        vec3 EvaluatePhysicalSkyForBake(SkyPacked s, vec3 dir)
        {
            SkyAtmParams atm = SkyMakeAtmParams(UnpackMediumRayleigh(s), UnpackMediumMie(s),
                                                UnpackMediumMieAbsorption(s), UnpackMediumOzone(s),
                                                UnpackMediumGround(s), UnpackMediumTentPlanet(s));

            vec3 originKm       = vec3(0.0f, atm.BottomRadiusKm + kBakeAnchorAltitudeKm, 0.0f);
            vec3 sunDir         = UnpackSunDirection(s);
            vec3 sunIlluminance = UnpackSkyConfig(s).sunColor * UnpackSunIntensity(s);

            SkyScatterResult result = SkyIntegrateScatteredLuminance(atm, originKm, dir, sunDir,
                                                                     sunIlluminance, kBakeSampleCount);

            vec3 sky = result.Luminance * UnpackSkyAndAerialPerspectiveLuminanceFactor(s);

            // Below the horizon: the lit ground under the marched air — the same v1 formula the screen
            // pass applies, with the view transmittance coming from this texel's own march.
            float r0 = originKm.y;
            if (SkyIntersectsGround(r0, dir.y, atm.BottomRadiusKm))
            {
                float sunZenithCos = clamp(sunDir.y, -1.0f, 1.0f);
                vec3  sunT         = texture(u_TransmittanceLut,
                                             SkyTransmittanceLutUvFromParams(
                                                  atm.BottomRadiusKm, atm.TopRadiusKm,
                                                  atm.BottomRadiusKm + SKY_PLANET_RADIUS_OFFSET_KM,
                                                  sunZenithCos)).rgb;
                float viewT = (result.Transmittance.r + result.Transmittance.g +
                               result.Transmittance.b) / 3.0f;
                sky += SkyGroundLuminance(atm, sunIlluminance, sunT, sunZenithCos) * viewT;
            }

            // The screen-pixel tint applies here too: the reflection of the sky must be the sky.
            return sky * UnpackSkyLuminanceFactor(s);
        }

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

            vec3 color;
            if (UnpackSkyModelIsPhysical(s))
                color = EvaluatePhysicalSkyForBake(s, dir);
            else
                color = EvaluateSky(dir, UnpackSunDirection(s), UnpackSunIntensity(s),
                                    UnpackSunAngularRadius(s), UnpackSkyConfig(s));

            imageStore(outputPanorama, coord, vec4(color, 1.0));
        }
    }
}
