Shader "CloudRaymarch"
{
    Compute
    {
        // Stage S2 of the volumetric clouds: THE RAYMARCH.
        //
        // One dispatch, one ray per pixel of a target scaled by Resolution Scale. Writes RGBA16F:
        // .rgb = in-scattered radiance, PREMULTIPLIED, linear HDR; .a = transmittance along the ray.
        // The composite then needs no more than `scene = cloud.rgb + scene * cloud.a`.
        //
        // LINEAR HDR ONLY. No tonemap, no gamma, no exposure — the engine's tonemap owns the curve and
        // runs later in the frame. The reference tonemapped inside its sky model AND again in its post
        // pass, and composited the two non-linearly (RESEARCH_REFERENCE J.3 #13).
        //
        // WHAT IS WHERE. Everything that can be a pure function of numbers is in
        // Common/CloudGeometry.glslh, which the CloudMath unit tests compile as C++ from this same text:
        // the shell intersection, the step schedule, the empty-space state machine, Beer, powder, the
        // phase functions, the in-scatter height term, the multi-scatter octaves, the cone offsets and
        // the depth reconstruction. Everything that reads an image is behind
        // Common/CloudDensityProcedural.glslh. What is left here is the loop that puts them together.
        //
        // OCCLUSION is resolved INSIDE this march, by clamping the ray to the distance the scene depth
        // attachment reports. One path for Forward and Deferred: both write that attachment, only
        // Deferred has a G-buffer. The composite therefore needs no depth test at all.

        #include <Common/Atmosphere.glslh>
        #include <Common/CloudNoise.glslh>
        #include <Common/CloudGeometry.glslh>
        #include <Common/CloudParams.glslh>
        #include <Common/CloudTemporal.glslh>
        #include <Common/CloudShadow.glslh>
        #include <Common/CloudDensityProcedural.glslh>

        // rgba16f: radiance is pre-tonemap HDR and transmittance is in [0,1]; half carries about three
        // decimal digits, which is an order of magnitude more than the 0.005 early-out needs.
        layout(binding = 0, rgba16f) restrict writeonly uniform image2D u_CloudScatter;

        // The DEPTH GUIDE for the composite's bilateral magnification: the distance this ray was allowed
        // to run to, packed into two rgba8 channels by CloudEncodeGuideDistance.
        //
        // It is written HERE, by the pass that already holds the number, rather than recomputed by a
        // second dispatch that would sample the scene depth all over again to learn what this one just
        // worked out. The composite cannot read the scene depth for itself — it draws into a framebuffer
        // that has the depth attachment bound — so somebody has to hand it across, and this is the
        // cheapest place: two lines and no extra fetches.
        layout(binding = 8, rgba8) restrict writeonly uniform image2D u_CloudDepthGuide;

        // The SAME sky parameter buffer the screen sky pass and the IBL bake read — bound by handle, not
        // rebuilt. Its layout lives in Common/Atmosphere.glslh and nothing here names a member of it, so
        // the sky's palette can change without touching this shader.
        ReadBuffer(1) SkyBuffer
        {
            vec4 u_SkyPacked[SKY_PACKED_VEC4_COUNT];
        };

        // The scene depth attachment, presented to this dispatch by ComputeImageBeginRead and handed
        // back afterwards. Point-sampled with texelFetch, never filtered: a filtered depth across a
        // silhouette averages foreground and background into a distance where nothing is.
        Uniform(7) sampler2D u_SceneDepth;

        // The sun-space shadow map: the optical depth toward the sun, precomputed once per column by
        // Programs/Clouds/CloudShadowMap.shader. Read in ONE fetch where the cone march took twelve.
        Uniform(9) sampler2D u_CloudShadowMap;

        PushConstant CloudPush
        {
            mat4 u_InverseViewProjection;
            vec4 u_CameraPosition; // xyz = camera position in world units, w = frame index
            vec4 u_Flags;          // x = 1 when the checkerboard is active (Full resolution + temporal)
        };

        LocalSize(8, 8, 1);

        // Interleaved gradient noise (Jimenez 2014), translated per frame. A per-pixel fraction of one
        // step is what turns the march's banding into noise the temporal stage can average away; without
        // it a 128-step march through a soft field draws visible shells.
        //
        // The per-frame translation is a HASH of the frame index, not a constant drift. IGN's isolines
        // are nearly vertical (the x coefficient is eleven times the y one), so shifting the pattern the
        // same way every frame leaves those isolines standing in the temporal average — a comb of faint
        // vertical streaks on every converged cloud edge, which is exactly what this replaces. Random
        // per-frame translations average the pattern over its own period and the comb goes; a single
        // frame (Temporal Off) still sees pure IGN, whose spatial quality is the reason it is used.
        float CloudJitter(vec2 pixel, float frame)
        {
            uint h = uint(frame) * 0x9E3779B1u;
            h ^= h >> 16;
            h *= 0x85EBCA6Bu;
            h ^= h >> 13;
            vec2 p = pixel + vec2(float(h & 0xFFu), float((h >> 8) & 0xFFu));
            return fract(52.9829189f * fract(0.06711056f * p.x + 0.00583715f * p.y));
        }

        // Depth of the nearest geometry covering this (possibly larger) cloud pixel. The MINIMUM over the
        // covered block, not one corner: erring toward the nearer surface makes clouds stop slightly
        // early at a silhouette, which reads as a clean edge, while erring the other way lets them bleed
        // over the object in front of them.
        float NearestSceneDepth(ivec2 coord, ivec2 targetSize)
        {
            ivec2 depthSize = textureSize(u_SceneDepth, 0);
            ivec2 last      = depthSize - ivec2(1, 1);
            ivec2 lo        = ivec2(vec2(coord) * vec2(depthSize) / vec2(targetSize));
            ivec2 hi        = min(lo + ivec2(1, 1), last);
            lo              = min(lo, last);

            float a = texelFetch(u_SceneDepth, ivec2(lo.x, lo.y), 0).r;
            float b = texelFetch(u_SceneDepth, ivec2(hi.x, lo.y), 0).r;
            float c = texelFetch(u_SceneDepth, ivec2(lo.x, hi.y), 0).r;
            float d = texelFetch(u_SceneDepth, ivec2(hi.x, hi.y), 0).r;
            return min(min(a, b), min(c, d));
        }

        void main()
        {
            ivec2 size  = imageSize(u_CloudScatter);
            ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
            if (coord.x >= size.x || coord.y >= size.y)
                return;

            // Fully transparent is the answer to every early-out below, and it has to be WRITTEN: the
            // target is not cleared between frames, so a pixel that returns without storing keeps last
            // frame's cloud and smears it across the sky as the camera turns.
            vec4 result = vec4(0.0f, 0.0f, 0.0f, 1.0f);

            vec2 uv  = (vec2(coord) + vec2(0.5f, 0.5f)) / vec2(size);
            vec2 ndc = vec2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

            // The ray from the inverse view-projection at two depths rather than from a hand-built
            // camera basis: it inherits whatever projection the camera has (perspective or orthographic,
            // any near/far) and cannot disagree with the matrix the rest of the frame was drawn with.
            vec4 nearH = u_InverseViewProjection * vec4(ndc.x, ndc.y, 0.0f, 1.0f);
            vec4 farH  = u_InverseViewProjection * vec4(ndc.x, ndc.y, 1.0f, 1.0f);
            vec3 nearP = nearH.xyz / nearH.w;
            vec3 farP  = farH.xyz / farH.w;
            vec3 dir   = normalize(farP - nearP);

            vec3 cameraPos = u_CameraPosition.xyz;

            float planetRadiusKm = CloudKmFromWorld(u_PlanetRadius);
            float bottomKm       = CloudKmFromWorld(u_LayerBottomAltitude);
            float thicknessKm    = CloudKmFromWorld(u_LayerThickness);
            vec3  originKm       = cameraPos * (1.0f / CLOUD_WORLD_UNITS_PER_KM);

            // Computed before the shell test and stored unconditionally: the guide describes where the
            // GEOMETRY is, which is true whether or not this ray met the cloud layer, and a pixel that
            // returned early without writing it would leave the composite weighting today's colours by a
            // distance from some earlier frame.
            float geometryLimit = CloudGeometryLimit(u_InverseViewProjection, cameraPos, ndc,
                                                     NearestSceneDepth(coord, size), u_MaxViewDistance);
            imageStore(u_CloudDepthGuide, coord, CloudEncodeGuideDistance(geometryLimit));

            // THE CHECKERBOARD (Full resolution + temporal only — see CloudCheckerboardActive on the
            // CPU side). A stale pixel does not march and does NOT write the scatter target: its texel
            // keeps the value marched last frame, and the temporal resolve replaces it on screen with
            // the reprojected, clamped history. The guide above IS still written — it describes where
            // the GEOMETRY is, which is a fact about this frame's depth buffer, not about the cloud.
            if (u_Flags.x > 0.5f && !CloudCheckerboardFresh(coord, uint(u_CameraPosition.w)))
                return;

            CloudShellHit shell = CloudShellBounds(originKm, dir, planetRadiusKm, bottomKm, thicknessKm);
            if (!shell.Hit)
            {
                imageStore(u_CloudScatter, coord, result);
                return;
            }

            float tEnter = CloudWorldFromKm(shell.TEnter);
            float tExit  = min(CloudWorldFromKm(shell.TExit), geometryLimit);
            if (tExit <= tEnter)
            {
                imageStore(u_CloudScatter, coord, result);
                return;
            }

            // Dither the entry point by a fraction of one step. Applied to the START and not to each
            // step, so the schedule stays the tested one.
            //
            // Dithered over the COARSE stride, not the fine one. The lattice that decides where a cloud
            // BEGINS is the coarse tier's, and its period is CoarseStepMultiplier times the fine stride
            // (CloudMarchAdvance: `coarse = stride * coarseMultiplier`) — the fine lattice is merely
            // anchored to it by the one-stride back-step. Dithering by a fine stride therefore spread the
            // start over only a THIRD of the period that matters, and a box filter of width P/3 over a
            // periodic error of period P leaves sin(60 deg)/(pi/3) = 83% of it standing. That residue is
            // what stacked into flat horizontal slabs above the horizon, where a grazing ray's entry
            // distance changes so fast per pixel row that the lattice phase beats against the pixel grid
            // every two or three rows. Over the full period the average is unbiased and the banding goes.
            float jitter = CloudJitter(vec2(coord), u_CameraPosition.w) * clamp(u_JitterStrength, 0.0f, 1.0f);
            float tStart = tEnter + jitter *
                                         CloudStepLength(tEnter, u_MinStepSize, u_MaxStepSize,
                                                         u_StepGrowthRate) *
                                         max(u_CoarseStepMultiplier, 1.0f);

            vec3  sunDir   = u_SunDirection.xyz;
            float cosTheta = dot(dir, sunDir);
            vec3  sunColour = u_SunIrradiance.xyz * u_SunIrradiance.w * u_SunTint.xyz;
            float sigmaScale = u_ExtinctionTint.w * CLOUD_EXTINCTION_PER_WORLD_UNIT;

            // The cone toward the sun is the SAME for every shaded sample on this ray: its basis, its
            // golden-angle spiral and its segment weights depend on sunDir and the authored distance and
            // spread, never on where along the ray we are. It used to be rebuilt at every shaded sample —
            // two crosses, a normalize, sin, cos, sqrt and four exp() per cone sample, times up to
            // sixteen samples, times up to 128 steps. Built once here instead; the values are identical.
            int coneCount = clamp(u_LightMarchSamples, 1, CLOUD_MAX_LIGHT_MARCH_SAMPLES);
            vec3  coneOffset[CLOUD_MAX_LIGHT_MARCH_SAMPLES];
            float coneWeight[CLOUD_MAX_LIGHT_MARCH_SAMPLES];
            for (int s = 0; s < coneCount; ++s)
            {
                coneOffset[s] = CloudConeSampleOffset(sunDir, s, coneCount, u_LightMarchDistance,
                                                      u_LightConeSpread);
                coneWeight[s] = CloudConeSampleWeight(s, coneCount, u_LightMarchDistance);
            }

            float transmittance = 1.0f;
            vec3  scattered     = vec3(0.0f, 0.0f, 0.0f);

            // Where the cloud this ray sees actually IS, weighted by how much each sample contributed.
            // The fades below used tEnter — the distance at which the RAY meets the shell — which is a
            // function of view elevation and not of the cloud: on a grazing ray a cloud a hundred
            // kilometres further on was treated as being at the shell's near edge.
            float visibleWeight   = 0.0f;
            float visibleDistance = 0.0f;

            // The march starts at the DITHERED position; tEnter stays the ray's true entry and is what
            // the horizon and distance fades below are measured against, so those keep a value that does
            // not carry a per-pixel dither.
            CloudMarchState state = CloudMarchBegin(tStart);

            // A `for` with MaxSteps as its bound, never a `while` over a procedural field: an
            // unbounded loop is how one bad parameter combination becomes a GPU hang instead of a bad
            // picture. The early-out on transmittance is a separate condition on a genuine
            // transmittance, which is the whole reason it fires at all.
            // Whether the loop ended because the ray left the shell (or went opaque) or because it simply
            // ran out of budget. The two are not the same picture and the difference is visible: a ray that
            // exhausts MaxSteps mid-cloud stops accumulating and writes whatever partial transmittance it
            // had, which draws a hard edge wherever the budget happens to run out. Near-vertical views hit
            // it first, and it reads as a comb of streaks hanging off cloud bases.
            bool finished = false;

            for (int marchStep = 0; marchStep < u_MaxSteps; ++marchStep)
            {
                if (state.T >= tExit || transmittance < 0.005f)
                {
                    finished = true;
                    break;
                }

                vec3  worldPos = cameraPos + dir * state.T;
                vec3  posKm    = worldPos * (1.0f / CLOUD_WORLD_UNITS_PER_KM);
                float height   = CloudHeightFraction(posKm, planetRadiusKm, bottomKm, thicknessKm);

                bool occupied = false;

                if (!state.Fine)
                {
                    // Coarse tier: cheap density only, and nothing is shaded. This is the empty-space
                    // skip, and it is the reason a 150 km ray is affordable without an SDF.
                    occupied = CloudDensityCheap(worldPos, height) > 0.0f;
                }
                else
                {
                    // One evaluation returns the eroded density AND the unerroded profile: the profile
                    // feeds the in-scatter probability and the ambient occlusion below (CLD-106), and a
                    // separate CloudDensityCheap call here was two texture fetches per shaded sample.
                    vec2  densitySample = CloudDensitySample(worldPos, height, state.T);
                    float density       = densitySample.x;
                    float profile       = densitySample.y;
                    occupied            = density > 0.0f;

                    if (occupied)
                    {
                        float dt = CloudStepLength(state.T, u_MinStepSize, u_MaxStepSize, u_StepGrowthRate);

                        // --- optical depth toward the sun ---
                        float sunDensityLength = 0.0f;

                        // The map answers for any sample inside its extent; anything beyond it, and the
                        // whole thing when it is switched off, falls back to the cone below. The fallback
                        // is not a rare path — the map is centred on the camera, so the far half of a
                        // 150 km shell is always outside it — which is why the cone stays.
                        vec2 shadowUv = CloudShadowUv(worldPos, cameraPos, sunDir, u_CloudShadowExtent);
                        bool shadowed = u_CloudShadowEnabled != 0 && CloudShadowInside(shadowUv);
                        if (shadowed)
                        {
                            // Read over the SAME reach the cone marches, not the whole column to the
                            // layer top. The two have to measure the same thing or the map's boundary is
                            // a brightness cliff in the sky: the column is the full optical depth to the
                            // sun and the cone is Light March Distance of it, which for a thick cloud is
                            // exp(-8.75) against exp(-2.5) — a few hundred times, on one texel's width.
                            // Truncating the map is the direction that keeps the presets meaning what
                            // they meant when they were authored, and keeps Light March Distance a knob
                            // that does something inside the extent instead of one that lies.
                            //
                            // Travelling LightMarchDistance along the sun lifts a sample by that distance
                            // times the sun's own height, which in layer fractions is the step below.
                            vec4  slices = textureLod(u_CloudShadowMap, shadowUv, 0.0f);
                            float reach  = u_LightMarchDistance * max(sunDir.y, 0.0f) /
                                           max(u_LayerThickness, 1.0f);
                            float above  = min(height + reach, 1.0f);

                            sunDensityLength = max(CloudShadowDensityLength(slices, height) -
                                                        CloudShadowDensityLength(slices, above),
                                                   0.0f);
                        }

                        for (int s = 0; !shadowed && s < coneCount; ++s)
                        {
                            vec3  samplePos = worldPos + coneOffset[s];
                            vec3  sampleKm  = samplePos * (1.0f / CLOUD_WORLD_UNITS_PER_KM);
                            float sampleH  = CloudLayerHeight(sampleKm, planetRadiusKm, bottomKm,
                                                              thicknessKm);

                            // A cone sample that has left the layer contributes nothing. Clamping its
                            // height instead would place it exactly on the surface it flew past, and
                            // every cloud top would then shadow itself with a copy of itself.
                            if (sampleH < 0.0f || sampleH > 1.0f)
                                continue;

                            sunDensityLength += CloudDensityCheap(samplePos, sampleH) * coneWeight[s];
                        }
                        float tauSun = sunDensityLength * sigmaScale;

                        // --- direct light: multi-scattered Beer x dual-lobe phase, powder, height ---
                        //
                        // The octaves march an optical depth that is DEPTH-MODULATED (CLD-113): Nubis3
                        // p.136 makes the effective extinction of the multiple-scattering estimate fall
                        // from 0.25 at the surface to 0.05 in the core, and hands the reduction out only
                        // toward the sun. Passing the raw tauSun — the collimated beam's own optical depth
                        // — to a term that is by definition NOT collimated is what rendered p.135's
                        // charcoal cores instead of p.136's luminous ones. The profile costs nothing here:
                        // CLD-106 already pays for it in the one density evaluation above.
                        vec3 energy = CloudMultiScatter(CloudMultiScatterOpticalDepth(tauSun, profile,
                                                                                      cosTheta),
                                                        u_ExtinctionTint.xyz, cosTheta,
                                                        u_MultiScatterOctaves,
                                                        u_MultiScatterExtinctionFalloff,
                                                        u_MultiScatterScatterFalloff,
                                                        u_MultiScatterPhaseFalloff, u_PhaseForwardG,
                                                        u_PhaseBackwardG, u_PhaseBlend,
                                                        u_SilverLiningIntensity);

                        // The published form takes a LOW-LOD density here — the UNERRODED profile — and
                        // v3 pays the two fetches to honour it (CLD-106). Passing the full density was
                        // the economy the previous comment defended, and the audit found its price: on a
                        // thin lit rim erosion drives the full density toward zero, the depth probability
                        // collapses to its 0.05 floor, and the in-scatter term crushed exactly the
                        // silver-lining rim the phase function was building. The profile is also what
                        // the ambient occlusion below is defined on (Nubis3 p.141), so one fetch serves
                        // both — and, since CLD-113, the depth modulation of the direct term too.
                        float inScatter = CloudInScatterProbability(height, profile, tauSun);

                        // Powder fades out toward the sun (CLD-107) — the dark edge is a reflection-side
                        // effect and must not dim the forward-scattered rim; see CloudPowderView.
                        float powder    = CloudPowderView(density, u_PowderStrength, u_PowderScale, cosTheta);

                        // The sky a sample can SEE, not the sky there is. Unoccluded ambient lights a
                        // cloud's core as brightly as its rim, which is exactly how a volume renders as a
                        // slab — see CloudAmbientOcclusion and Nubis3 pp. 141/144.
                        //
                        // The column term reads the FULL stack above the sample out of the shadow map,
                        // untruncated: the direct term is capped at Light March Distance because it has to
                        // agree with the cone that answers outside the map, but ambient occlusion is about
                        // everything overhead and has no second implementation to agree with.
                        //
                        // The map accumulates along the SUN; sky occlusion is about the stack OVERHEAD, so
                        // the slant length is projected onto the vertical (CLD-103, CloudAmbientColumnVertical)
                        // — without it a sunset sun reported a column 1/sin(elevation) too long and ambient
                        // died exactly when it was the only light left. The trust in the map's answer also
                        // fades over the outer tenth of its extent (CloudShadowEdgeFade): a term that
                        // stops at the map's edge draws that edge in the sky as a brightness step.
                        float columnAbove = 0.0f;
                        if (shadowed)
                            columnAbove = CloudAmbientColumnVertical(
                                 CloudShadowDensityLength(
                                      textureLod(u_CloudShadowMap, shadowUv, 0.0f), height),
                                 sunDir.y) *
                                 CloudShadowEdgeFade(shadowUv);

                        // ShadowTint means SHADOW (CLD-105): weighted by sun occlusion, so a lit top
                        // keeps the sky's own colour and only the shaded side takes the authored tint —
                        // see CloudShadowTintWeight, which the CloudMath tests drive as C++.
                        vec3 shadowTint = CloudShadowTintWeight(u_ShadowTint.xyz, tauSun);

                        vec3 ambient = CloudAmbient(u_ZenithRadiance.xyz, u_GroundRadiance.xyz,
                                                    u_ZenithRadiance.w, u_GroundRadiance.w,
                                                    u_ScatteringAlbedo.w, height) *
                                       shadowTint *
                                       CloudAmbientOcclusion(profile, columnAbove, sigmaScale, u_AmbientOcclusion);

                        // Rain darkens a cloud from the base up, and only where the weather map says it
                        // is raining — a uniform darkening would just be a brightness slider.
                        float wet     = CloudPrecipitationAt(worldPos);
                        float darken  = 1.0f - u_SunTint.w * wet * (1.0f - height);
                        vec3  albedo  = u_ScatteringAlbedo.xyz * max(darken, 0.0f);

                        vec3 scattering = albedo * (sunColour * (energy * inScatter * powder) + ambient);

                        // Analytic in-scatter integration over the step — see CloudIntegrateInScatter,
                        // which is where the reasoning and the energy bound live, and which the CloudMath
                        // tests drive as C++. This site used to divide by sigma, leaving a spurious
                        // 1/sigma_t (per CENTIMETRE, so about five thousand) in every cloud's radiance:
                        // the clouds saturated to flat white at any exposure while the silhouette stayed
                        // correct, because transmittance is accumulated separately and was never wrong.
                        float sigma      = max(density * sigmaScale, 1e-9f);
                        float stepTrans  = CloudBeerTransmittance(sigma * dt);
                        vec3  integrated = CloudIntegrateInScatter(scattering, sigma, dt);

                        scattered += transmittance * integrated;

                        // Weighted by what this sample actually adds to the pixel, so a faint haze in
                        // front does not outvote the cloud the eye is looking at.
                        float contribution = transmittance * (1.0f - stepTrans);
                        visibleWeight += contribution;
                        visibleDistance += contribution * state.T;

                        transmittance *= stepTrans;
                    }
                }

                state = CloudMarchAdvance(state, occupied, tStart, u_MinStepSize, u_MaxStepSize,
                                          u_StepGrowthRate, u_CoarseStepMultiplier,
                                          u_EmptySamplesBeforeCoarse);
            }

            // Budget exhausted mid-shell: close the ray out rather than leaving it half-integrated. Every
            // step still owed would have added SOMETHING, so the honest cheap stand-in is to let the
            // remaining transmittance decay by what the traversed fraction suggests, which turns a hard cut
            // into a fade. The alternative — leaving it — is the comb of streaks this replaces.
            if (!finished)
            {
                float travelled = clamp((state.T - tStart) / max(tExit - tStart, 1e-3f), 0.0f, 1.0f);
                float owed      = 1.0f - travelled;
                transmittance   = mix(transmittance, 1.0f, owed * owed);
                scattered       = scattered * (1.0f - owed * owed);
            }

            // Horizon dissolve: the far edge of the layer fades into the sky instead of ending on the
            // hard circle where the shell meets the horizon.
            // The distance the fades are measured at: where the cloud is, falling back to the shell entry
            // for a ray that met nothing (there is no cloud whose distance to use).
            float cloudDistance = visibleWeight > 1e-6f ? visibleDistance / visibleWeight : tEnter;

            // Auto ranges are derived from the layer's own geometry; see CloudAutoFadeStart/End.
            float fadeStart    = u_AutoDistanceFade != 0 ? CloudAutoFadeStart(u_LayerBottomAltitude)
                                                         : u_DistanceFadeStart;
            float fadeEnd      = u_AutoDistanceFade != 0
                                      ? CloudAutoFadeEnd(u_PlanetRadius, u_LayerBottomAltitude, u_LayerThickness)
                                      : u_DistanceFadeEnd;
            float horizonStart = u_AutoDistanceFade != 0 ? fadeStart * 4.0f : u_HorizonFadeStart;
            float horizonEnd   = u_AutoDistanceFade != 0 ? fadeEnd : u_HorizonFadeEnd;

            float horizon = 1.0f - CloudRemapRange(cloudDistance, horizonStart, horizonEnd, 0.0f, 1.0f);
            scattered *= horizon;
            transmittance = mix(1.0f, transmittance, horizon);

            // Atmospheric perspective: distant clouds take the colour of the air in front of them. The
            // sky is EVALUATED here through the shared model, in the exact direction this ray points —
            // not interpolated between transported probe colours, and not a palette this shader reads.
            SkyPacked sky;
            for (int i = 0; i < SKY_PACKED_VEC4_COUNT; ++i)
                sky.v[i] = u_SkyPacked[i];

            // Evaluated with a sun INTENSITY of zero, deliberately. Aerial perspective is the light the
            // air in front of the cloud scatters toward the camera: the sky gradient and the sun's broad
            // halo belong in it, the solar DISC does not. Leaving the disc in would paint a small
            // blinding spot onto any distant cloud that happened to cross the sun. Zeroing the intensity
            // removes exactly the disc term (core * sunIntensity) and leaves the halo, which is scaled
            // by sunGlow instead.
            vec3 skyColour = EvaluateSky(dir, UnpackSunDirection(sky), 0.0f, UnpackSunAngularRadius(sky),
                                         UnpackSkyConfig(sky));

            float aerial = CloudRemapRange(cloudDistance, fadeStart, fadeEnd, 0.0f, 1.0f) *
                           clamp(u_ShadowTint.w, 0.0f, 1.0f);
            scattered = mix(scattered, skyColour * (1.0f - transmittance), aerial);

            result = vec4(scattered, clamp(transmittance, 0.0f, 1.0f));
            imageStore(u_CloudScatter, coord, result);
        }
    }
}
