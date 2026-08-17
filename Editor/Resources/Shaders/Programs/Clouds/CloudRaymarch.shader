Shader "CloudRaymarch"
{
    Compute
    {
        // Stage S2 of the volumetric clouds: THE RAYMARCH.
        //
        // TWO LAYERS, ONE RAY. A sky has a deck low down and a thin sheet high above it (Nubis3
        // pp. 125/126/150), and one shell cannot be both. So this pass builds a PLAN — the shells this
        // ray crosses, in the order it crosses them, with no interval marched twice — and walks it with a
        // single transmittance accumulator. That is what makes "near over far" true by construction: no
        // second dispatch, no second target, no sort, and nothing to composite afterwards. Each layer
        // brings its own weather, shape, lighting, animation, step schedule and step BUDGET, so the sheet
        // above cannot be starved by the deck the ray met first.
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
        // the depth reconstruction. Everything that reads an image is behind the density seam,
        // Common/CloudDensityCompose.glslh — which unions the procedural cloudscape with whatever baked
        // hero clouds the scene placed. What is left here is the loop that puts them together, and it did
        // not have to change to gain the second density model.
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

        // THE VIEW marches every hero cloud the scene placed. (The shadow pass marches only the prefix
        // that casts cloud shadows — see CloudShadowMap.shader.)
        #define CLOUD_VOXEL_INSTANCE_COUNT u_VoxelInstanceCount
        #include <Common/CloudDensityCompose.glslh>

        // The aerial-perspective volume's SLICE MAPPING only — the same include, for the same reason, as
        // Programs/Fog/HeightFog.shader: SkyScattering.glslh's integrator block is guarded on the two LUT
        // callback macros, which this pass deliberately does not define. It READS the volume, it never
        // fills it, so the only thing it needs from the sky's maths is the exact inverse of the mapping
        // the fill wrote through. One mapping, three consumers, no chance of a slice drifting apart.
        #include <Common/SkyMedium.glslh>
        #include <Common/SkyScattering.glslh>

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
        // One SLICE per cloud layer — a layer self-shadows with its own mass, and the four heights a
        // texel stores are heights inside that layer.
        Uniform(9) sampler3D u_CloudShadowMap;

        // THE SKY'S CAMERA AERIAL-PERSPECTIVE VOLUME (Graphic::kCloudAerialPerspectiveBinding): 32x32x16
        // froxels of THIS camera's frustum, each holding the air between the eye and that distance —
        // rgb premultiplied in-scatter, a the mean transmittance. Filled by
        // Programs/Sky/SkyAerialPerspectiveLut.shader earlier in this same frame.
        //
        // Sampled TRILINEARLY and exactly ONCE per pixel, at the transmittance-weighted mean depth of the
        // cloud this ray saw. The alternative — the air integrated per shaded sample — is a volume fetch
        // inside a 128-step loop for a quantity that varies over kilometres, and the cloud's own march
        // already knows where its mass is.
        Uniform(12) sampler3D u_AerialPerspective;

        // THE SKY'S DISTANT SKY LIGHT (Graphic::kCloudDistantSkyLightBinding): two texels holding the
        // average radiance of the sky, marched this frame from 64 directions at 6 km
        // (Programs/Sky/SkyDistantLight.shader). The march reads texel 1, the SKY HALF — the mean over
        // the 32 directions that look UP (Graphic::kDistantLightSkyTexel).
        //
        // Not texel 0, the full-sphere mean the height fog reads: half of that is the lit ground, and
        // CloudAmbient already has a ground-bounce term of its own. Feeding it the sphere mean would
        // count the ground twice and hand every shadowed cloud face a near-achromatic grey (measured
        // R/B 0.98) instead of the blue of the sky it hangs against (0.27).
        Uniform(13) sampler2D u_DistantSkyLight;

        PushConstant CloudPush
        {
            mat4 u_InverseViewProjection;
            vec4 u_CameraPosition; // xyz = camera position in world units, w = frame index
            vec4 u_Flags;          // x = 1 when the checkerboard is active (Full resolution + temporal)
            // Mirrored by Graphic::CloudRaymarchPush::Atmosphere — see it for the contract:
            // x = AP volume depth (km), y = view-distance scale, z = 1 when the volume exists,
            // w = 1 when the distant sky light exists.
            vec4 u_Atmosphere;
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

        // ---- One segment of the plan ----------------------------------------------------------------
        //
        // THE ACCUMULATORS ARE GLOBALS. A segment composites into the ray's running total and GLSL has no
        // references, so the totals are per-invocation globals rather than out-parameters — which the
        // optimiser promotes to plain values the moment this function is inlined.
        //
        // The ACTIVE LAYER is not a parameter: it is whatever the caller last selected, and where that
        // selection happens is what decides the cost of this whole pass. See the note on the two loops in
        // main(), and the measurements in Common/CloudParams.glslh.
        float g_Transmittance;
        vec3  g_Scattered;
        float g_VisibleWeight;
        float g_VisibleDistance;
        float g_DominantWeight;
        int   g_DominantLayer;

        // EACH LAYER'S OWN STEP BUDGET, as two named scalars rather than an array so that the slot folds
        // with the index too. Not one budget for the ray: near the horizon a deck's shell is a hundred
        // kilometres of coarse striding, and a shared budget would be spent before the ray ever reached
        // the sheet above it — cirrus would vanish at exactly the elevation where a real sky shows most
        // of it. A layer that owns two segments (the contained case) spends one budget across both, so
        // the worst case is still MaxSteps per LAYER.
        int g_LayerBudget0;
        int g_LayerBudget1;

        void CloudMarchOneSegment(CloudMarchSegment segment, ivec2 coord, vec3 cameraPos, vec3 dir,
                                  vec3 sunDir, float cosTheta, vec3 ambientSky, float planetRadiusKm,
                                  float geometryLimit)
        {
            float bottomKm    = CloudKmFromWorld(u_LayerBottomAltitude);
            float thicknessKm = CloudKmFromWorld(u_LayerThickness);

            // Clipped by the scene depth and by THIS layer's own Max View Distance. A segment that
            // closes to nothing contributes nothing and leaves every accumulator untouched, which is
            // what returning here means — the caller goes on to the next segment.
            float tEnter = CloudWorldFromKm(segment.TEnter);
            float tExit  = min(CloudWorldFromKm(segment.TExit), min(geometryLimit, u_MaxViewDistance));
            if (tExit <= tEnter)
                return;

            // Dither the entry point by a fraction of one step. Applied to the START and not to each
            // step, so the schedule stays the tested one.
            //
            // Dithered over the COARSE stride, not the fine one. The lattice that decides where a
            // cloud BEGINS is the coarse tier's, and its period is CoarseStepMultiplier times the
            // fine stride (CloudMarchAdvance: `coarse = stride * coarseMultiplier`) — the fine
            // lattice is merely anchored to it by the one-stride back-step. Dithering by a fine
            // stride therefore spread the start over only a THIRD of the period that matters, and a
            // box filter of width P/3 over a periodic error of period P leaves sin(60 deg)/(pi/3) =
            // 83% of it standing. That residue is what stacked into flat horizontal slabs above the
            // horizon, where a grazing ray's entry distance changes so fast per pixel row that the
            // lattice phase beats against the pixel grid every two or three rows. Over the full
            // period the average is unbiased and the banding goes.
            float jitter = CloudJitter(vec2(coord), u_CameraPosition.w) *
                           clamp(u_JitterStrength, 0.0f, 1.0f);
            float tStart = tEnter + jitter *
                                         CloudStepLength(tEnter, u_MinStepSize, u_MaxStepSize,
                                                         u_StepGrowthRate) *
                                         max(u_CoarseStepMultiplier, 1.0f);

            vec3  sunColour  = u_SunIrradiance.xyz * u_SunLightIntensityScale * u_SunTint.xyz;
            float sigmaScale = u_ExtinctionTint.w * CLOUD_EXTINCTION_PER_WORLD_UNIT;

            // The cone toward the sun is the SAME for every shaded sample in this segment: its basis,
            // its golden-angle spiral and its segment weights depend on sunDir and the layer's
            // authored distance and spread, never on where along the ray we are. It used to be
            // rebuilt at every shaded sample — two crosses, a normalize, sin, cos, sqrt and four
            // exp() per cone sample, times up to sixteen samples, times up to 128 steps. Built once
            // per segment instead; the values are identical.
            int   coneCount = clamp(u_LightMarchSamples, 1, CLOUD_MAX_LIGHT_MARCH_SAMPLES);
            vec3  coneOffset[CLOUD_MAX_LIGHT_MARCH_SAMPLES];
            float coneWeight[CLOUD_MAX_LIGHT_MARCH_SAMPLES];
            for (int s = 0; s < coneCount; ++s)
            {
                coneOffset[s] = CloudConeSampleOffset(sunDir, s, coneCount, u_LightMarchDistance,
                                                      u_LightConeSpread);
                coneWeight[s] = CloudConeSampleWeight(s, coneCount, u_LightMarchDistance);
            }

            // THE SEGMENT'S OWN accumulators, and the transmittance the ray arrived with.
            //
            // Front-to-back compositing, written as a prefix times a local integral rather than as
            // one running product, because the horizon dissolve below is PER LAYER: a sheet at 10 km
            // has a geometric horizon two hundred kilometres beyond a deck's, and fading both by one
            // range would dissolve whichever layer did not author it at the wrong distance. With a
            // single layer the prefix is exactly 1 and every line below is the arithmetic this march
            // has always run.
            float prefix       = g_Transmittance;
            float segTrans     = 1.0f;
            vec3  segScattered = vec3(0.0f, 0.0f, 0.0f);
            float segWeight    = 0.0f;
            float segDistance  = 0.0f;

            // Density x step length, summed over every shaded sample: the ray's own measurement of
            // the mean extinction of the medium it crossed. Read only by CloudCloseExhaustedRay.
            float marchedDensityLength = 0.0f;

            // The march starts at the DITHERED position; tEnter stays this segment's true entry and
            // is what the fades below are measured against, so those keep a value that does not
            // carry a per-pixel dither.
            CloudMarchState state = CloudMarchBegin(tStart);

            // A `for` with the layer's budget as its bound, never a `while` over a procedural field:
            // an unbounded loop is how one bad parameter combination becomes a GPU hang instead of a
            // bad picture. The early-out on transmittance is a separate condition on a genuine
            // transmittance, which is the whole reason it fires at all.
            //
            // Whether the loop ended because the ray left the shell (or went opaque) or because it
            // simply ran out of budget. The two are not the same picture and the difference is
            // visible: a ray that exhausts its budget mid-cloud stops accumulating and writes
            // whatever partial transmittance it had, which draws a hard edge wherever the budget
            // happens to run out. Near-vertical views hit it first, and it reads as a comb of streaks
            // hanging off cloud bases.
            bool finished = false;
            int  budget   = g_CloudLayerIndex == 0 ? g_LayerBudget0 : g_LayerBudget1;
            int  used     = 0;

            for (int marchStep = 0; marchStep < budget; ++marchStep)
            {
                if (state.T >= tExit || prefix * segTrans < 0.005f)
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
                    // One evaluation returns the eroded density AND the unerroded profile: the
                    // profile feeds the in-scatter probability and the ambient occlusion below
                    // (CLD-106), and a separate CloudDensityCheap call here was two texture fetches
                    // per shaded sample. The stride at this sample, computed once and used twice: it
                    // gates the near-field detail band (a statement about the sampling rate, not
                    // about the distance) and it is the dt the in-scatter integration runs over.
                    float dt = CloudStepLength(state.T, u_MinStepSize, u_MaxStepSize, u_StepGrowthRate);

                    vec2  densitySample = CloudDensitySample(worldPos, height, state.T, dt);
                    float density       = densitySample.x;
                    float profile       = densitySample.y;

                    // THE OCCUPANCY VERDICT IS THE PROFILE, NOT THE ERODED DENSITY — the same field the
                    // COARSE tier judges by, which is the whole point.
                    //
                    // The two tiers used to answer different questions: coarse asked the cheap density
                    // (weather x envelope x base shape) and fine asked the density AFTER erosion, which is
                    // zero over most of a cloud's interior because the detail threshold cuts it there. So
                    // a fine excursion inside a real cloud would count `emptyBeforeCoarse` erosion holes,
                    // decide the cloud had ended, hand control back to the coarse tier and jump a full
                    // coarse stride — 2.8 km at the horizon — over cloud the coarse tier itself calls
                    // occupied. WHERE those holes fall is set by the entry dither, so two neighbouring
                    // pixels skipped different kilometres of the same deck and integrated visibly
                    // different amounts of it. That is the ragged fringe: not a fade, not a budget, but a
                    // state machine whose trajectory is a chaotic function of a per-pixel random number.
                    //
                    // Judging both tiers by the profile makes the machine's trajectory a property of the
                    // FIELD alone: no interval the conservative test calls occupied is ever skipped, and
                    // the same ray with a different dither phase now differs only by the quadrature error
                    // it was always allowed. The erosion keeps its whole job — it still decides how much
                    // light every shaded sample scatters — it just no longer decides where the march goes.
                    occupied = profile > 0.0f;

                    // A fine excursion that has not yet found anything is still SEARCHING, at the
                    // coarse rate (CloudMarchAdvance). Its samples answer "is there cloud here" and
                    // nothing else: the sample that first says yes is followed by a rewind of one
                    // coarse stride, and the fine pass over that interval is what integrates it.
                    // Shading here as well would count this step twice — and would pay for a cone
                    // march at every step of the search, which is the cost the search exists to avoid.
                    // A sample the erosion emptied is INSIDE the cloud and contributes nothing: it is
                    // marched through, it keeps the excursion alive, and it is not shaded. Shading it
                    // would pay for a cone march and a shadow fetch to multiply by a density of zero.
                    if (occupied && state.Shaded && density > 0.0f)
                    {
                        // --- optical depth toward the sun ---
                        float sunDensityLength = 0.0f;

                        // The map answers for any sample inside its extent; anything beyond it, and
                        // the whole thing when it is switched off, falls back to the cone below. The
                        // fallback is not a rare path — the map is centred on the camera, so the far
                        // half of a 150 km shell is always outside it — which is why the cone stays.
                        vec2 shadowUv = CloudShadowUv(worldPos, cameraPos, sunDir, u_CloudShadowExtent);
                        vec3 shadowCoord = vec3(shadowUv.x, shadowUv.y, CloudActiveLayerSliceW());
                        bool shadowed = u_CloudShadowEnabled != 0 && CloudShadowInside(shadowUv);
                        if (shadowed)
                        {
                            // Read over the SAME reach the cone marches, not the whole column to the
                            // layer top. The two have to measure the same thing or the map's boundary
                            // is a brightness cliff in the sky: the column is the full optical depth
                            // to the sun and the cone is Light March Distance of it, which for a thick
                            // cloud is exp(-8.75) against exp(-2.5) — a few hundred times, on one
                            // texel's width. Truncating the map is the direction that keeps the
                            // presets meaning what they meant when they were authored, and keeps
                            // Light March Distance a knob that does something inside the extent
                            // instead of one that lies.
                            //
                            // Travelling LightMarchDistance along the sun lifts a sample by that
                            // distance times the sun's own height, which in layer fractions is the
                            // step below.
                            vec4  slices = textureLod(u_CloudShadowMap, shadowCoord, 0.0f);
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
                        // The octaves march an optical depth that is DEPTH-MODULATED (CLD-113):
                        // Nubis3 p.136 makes the effective extinction of the multiple-scattering
                        // estimate fall from 0.25 at the surface to 0.05 in the core, and hands the
                        // reduction out only toward the sun. Passing the raw tauSun — the collimated
                        // beam's own optical depth — to a term that is by definition NOT collimated is
                        // what rendered p.135's charcoal cores instead of p.136's luminous ones. The
                        // profile costs nothing here: CLD-106 already pays for it in the one density
                        // evaluation above.
                        vec3 energy = CloudMultiScatter(CloudMultiScatterOpticalDepth(tauSun, profile,
                                                                                      cosTheta),
                                                        u_ExtinctionTint.xyz, cosTheta,
                                                        u_MultiScatterOctaves,
                                                        u_MultiScatterExtinctionFalloff,
                                                        u_MultiScatterScatterFalloff,
                                                        u_MultiScatterPhaseFalloff, u_PhaseForwardG,
                                                        u_PhaseBackwardG, u_PhaseBlend,
                                                        u_SilverLiningIntensity);

                        // The published form takes a LOW-LOD density here — the UNERRODED profile —
                        // and v3 pays the two fetches to honour it (CLD-106). Passing the full density
                        // was the economy the previous comment defended, and the audit found its
                        // price: on a thin lit rim erosion drives the full density toward zero, the
                        // depth probability collapses to its 0.05 floor, and the in-scatter term
                        // crushed exactly the silver-lining rim the phase function was building. The
                        // profile is also what the ambient occlusion below is defined on (Nubis3
                        // p.141), so one fetch serves both — and, since CLD-113, the depth modulation
                        // of the direct term too.
                        float inScatter = CloudInScatterProbability(height, profile, tauSun);

                        // Powder fades out toward the sun (CLD-107) — the dark edge is a
                        // reflection-side effect and must not dim the forward-scattered rim; see
                        // CloudPowderView.
                        float powder = CloudPowderView(density, u_PowderStrength, u_PowderScale,
                                                       cosTheta);

                        // The sky a sample can SEE, not the sky there is. Unoccluded ambient lights a
                        // cloud's core as brightly as its rim, which is exactly how a volume renders
                        // as a slab — see CloudAmbientOcclusion and Nubis3 pp. 141/144.
                        //
                        // The column term reads the FULL stack above the sample out of the shadow map,
                        // untruncated: the direct term is capped at Light March Distance because it
                        // has to agree with the cone that answers outside the map, but ambient
                        // occlusion is about everything overhead and has no second implementation to
                        // agree with. It is also this LAYER's own stack: a sheet eight kilometres
                        // above the deck is not occluded by it, and the deck's map has no slice that
                        // could say so.
                        //
                        // The map accumulates along the SUN; sky occlusion is about the stack
                        // OVERHEAD, so the slant length is projected onto the vertical (CLD-103,
                        // CloudAmbientColumnVertical) — without it a sunset sun reported a column
                        // 1/sin(elevation) too long and ambient died exactly when it was the only
                        // light left. The trust in the map's answer also fades over the outer tenth of
                        // its extent (CloudShadowEdgeFade): a term that stops at the map's edge draws
                        // that edge in the sky as a brightness step.
                        float columnAbove = 0.0f;
                        if (shadowed)
                            columnAbove = CloudAmbientColumnVertical(
                                 CloudShadowDensityLength(
                                      textureLod(u_CloudShadowMap, shadowCoord, 0.0f), height),
                                 sunDir.y) *
                                 CloudShadowEdgeFade(shadowUv);

                        // ShadowTint means SHADOW (CLD-105): weighted by sun occlusion, so a lit top
                        // keeps the sky's own colour and only the shaded side takes the authored tint
                        // — see CloudShadowTintWeight, which the CloudMath tests drive as C++.
                        vec3 shadowTint = CloudShadowTintWeight(u_ShadowTint.xyz, tauSun);

                        vec3 ambient = CloudAmbient(ambientSky, u_GroundRadiance.xyz,
                                                    u_AmbientSkyContribution,
                                                    u_AmbientGroundContribution,
                                                    u_ScatteringAlbedo.w, height) *
                                       shadowTint *
                                       CloudAmbientOcclusion(profile, columnAbove, sigmaScale,
                                                             u_AmbientOcclusion);

                        // Rain darkens a cloud from the base up, and only where the weather map says
                        // it is raining — a uniform darkening would just be a brightness slider.
                        float wet     = CloudPrecipitationAt(worldPos);
                        float darken  = 1.0f - u_SunTint.w * wet * (1.0f - height);
                        vec3  albedo  = u_ScatteringAlbedo.xyz * max(darken, 0.0f);

                        vec3 scattering = albedo * (sunColour * (energy * inScatter * powder) + ambient);

                        // Analytic in-scatter integration over the step — see CloudIntegrateInScatter,
                        // which is where the reasoning and the energy bound live, and which the
                        // CloudMath tests drive as C++. This site used to divide by sigma, leaving a
                        // spurious 1/sigma_t (per CENTIMETRE, so about five thousand) in every cloud's
                        // radiance: the clouds saturated to flat white at any exposure while the
                        // silhouette stayed correct, because transmittance is accumulated separately
                        // and was never wrong.
                        float sigma      = max(density * sigmaScale, 1e-9f);
                        float stepTrans  = CloudBeerTransmittance(sigma * dt);
                        vec3  integrated = CloudIntegrateInScatter(scattering, sigma, dt);

                        segScattered += segTrans * integrated;

                        // Weighted by what this sample actually adds to the pixel, so a faint haze in
                        // front does not outvote the cloud the eye is looking at.
                        float contribution = segTrans * (1.0f - stepTrans);
                        segWeight += contribution;
                        segDistance += contribution * state.T;
                        marchedDensityLength += density * dt;

                        segTrans *= stepTrans;
                    }
                }

                state = CloudMarchAdvance(state, occupied, tStart, u_MinStepSize, u_MaxStepSize,
                                          u_StepGrowthRate, u_CoarseStepMultiplier,
                                          u_EmptySamplesBeforeCoarse);
                used = marchStep + 1;
            }

            if (g_CloudLayerIndex == 0)
                g_LayerBudget0 = max(budget - used, 0);
            else
                g_LayerBudget1 = max(budget - used, 0);

            // Budget exhausted mid-shell: close the segment out with the mean extinction and mean
            // radiance it measured over the part it did march — see CloudCloseExhaustedRay, which is
            // where the reasoning and the energy bound live and which the CloudMath tests drive as
            // C++.
            if (!finished)
            {
                CloudRayTail tail = CloudCloseExhaustedRay(segScattered, segTrans,
                                                           marchedDensityLength, state.T - tStart,
                                                           tExit - state.T, sigmaScale);
                segScattered      = tail.Scattered;
                segTrans          = tail.Transmittance;
            }

            // The distance the fades are measured at: where this layer's cloud is, falling back to
            // the segment's entry for a ray that met nothing (there is no cloud whose distance to use).
            float segCloudDistance = segWeight > 1e-6f ? segDistance / segWeight : tEnter;

            // Auto ranges are derived from THIS layer's own geometry; see CloudAutoFadeStart/End.
            float fadeStart = u_AutoDistanceFade != 0 ? CloudAutoFadeStart(u_LayerBottomAltitude)
                                                      : u_DistanceFadeStart;
            float fadeEnd   = u_AutoDistanceFade != 0
                                   ? CloudAutoFadeEnd(u_PlanetRadius, u_LayerBottomAltitude,
                                                      u_LayerThickness)
                                   : u_DistanceFadeEnd;
            float horizonStart = u_AutoDistanceFade != 0 ? fadeStart * 4.0f : u_HorizonFadeStart;
            float horizonEnd   = u_AutoDistanceFade != 0 ? fadeEnd : u_HorizonFadeEnd;

            // Horizon dissolve: the far edge of the layer fades into the sky instead of ending on the
            // hard circle where the shell meets the horizon.
            float horizon = 1.0f - CloudRemapRange(segCloudDistance, horizonStart, horizonEnd,
                                                   0.0f, 1.0f);

            // Composite this segment OVER what is already there. With one layer prefix is 1 and these
            // four lines are exactly `g_Scattered *= horizon; g_Transmittance = mix(1, t, horizon)`.
            g_Scattered += prefix * segScattered * horizon;
            g_VisibleWeight += prefix * segWeight * horizon;
            g_VisibleDistance += prefix * segDistance * horizon;
            g_Transmittance = prefix * mix(1.0f, segTrans, horizon);

            float segContribution = prefix * segWeight * horizon;
            if (segContribution > g_DominantWeight)
            {
                g_DominantWeight = segContribution;
                g_DominantLayer  = g_CloudLayerIndex;
            }
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
            vec3  originKm       = cameraPos * (1.0f / CLOUD_WORLD_UNITS_PER_KM);

            int layerCount = clamp(u_LayerCount, 0, CLOUD_MAX_LAYERS);

            // The GUIDE describes where the GEOMETRY is, so its cap is the FURTHEST any layer would have
            // marched. A nearer layer's own Max View Distance clamps that LAYER's segments below; using
            // it here would tell the composite's bilateral upsample that the sky ends where the deck's
            // range does, and weight its taps by a distance no geometry is at.
            float maxViewDistance = 0.0f;
            for (int i = 0; i < layerCount; ++i)
                maxViewDistance = max(maxViewDistance, u_CloudLayers[i].MaxViewDistance);

            // Computed before the shell test and stored unconditionally: the guide describes where the
            // GEOMETRY is, which is true whether or not this ray met any cloud layer, and a pixel that
            // returned early without writing it would leave the composite weighting today's colours by a
            // distance from some earlier frame.
            float geometryLimit = CloudGeometryLimit(u_InverseViewProjection, cameraPos, ndc,
                                                     NearestSceneDepth(coord, size), maxViewDistance);
            imageStore(u_CloudDepthGuide, coord, CloudEncodeGuideDistance(geometryLimit));

            // THE CHECKERBOARD (Full resolution + temporal only — see CloudCheckerboardActive on the
            // CPU side). A stale pixel does not march and does NOT write the scatter target: its texel
            // keeps the value marched last frame, and the temporal resolve replaces it on screen with
            // the reprojected, clamped history. The guide above IS still written — it describes where
            // the GEOMETRY is, which is a fact about this frame's depth buffer, not about the cloud.
            if (u_Flags.x > 0.5f && !CloudCheckerboardFresh(coord, uint(u_CameraPosition.w)))
                return;

            // ---- THE PLAN ------------------------------------------------------------------------
            //
            // The shells this ray crosses, in the order it crosses them, with no interval marched twice.
            // A sky has a deck low down and a thin sheet high above it, and one ray meets them in an
            // order that depends on where the camera is: from the ground the deck first, from a
            // stratospheric camera the sheet first, and looking down at a shallow angle the sheet's own
            // conservative span can CONTAIN the deck's. CloudPlanTwoShells resolves all three into
            // ordered, disjoint segments, and marching them front to back with one transmittance
            // accumulator is what makes "near over far" true by construction rather than by a sort.
            CloudShellHit shellA;
            shellA.Hit    = false;
            shellA.TEnter = 0.0f;
            shellA.TExit  = 0.0f;
            CloudShellHit shellB = shellA;

            if (layerCount > 0)
                shellA = CloudShellBounds(originKm, dir, planetRadiusKm,
                                          CloudKmFromWorld(u_CloudLayers[0].LayerBottomAltitude),
                                          CloudKmFromWorld(u_CloudLayers[0].LayerThickness));
            if (layerCount > 1)
                shellB = CloudShellBounds(originKm, dir, planetRadiusKm,
                                          CloudKmFromWorld(u_CloudLayers[1].LayerBottomAltitude),
                                          CloudKmFromWorld(u_CloudLayers[1].LayerThickness));

            CloudMarchPlan plan = CloudPlanTwoShells(shellA, 0, shellB, 1);
            if (plan.Count == 0)
            {
                imageStore(u_CloudScatter, coord, result);
                return;
            }

            vec3  sunDir   = u_SunDirection.xyz;
            float cosTheta = dot(dir, sunDir);

            // THE SKY THE CLOUD'S SHADOWED SIDE IS LIT BY, chosen once per ray and not per sample: it is
            // one value for the whole frame in both models, so fetching it inside the march would be the
            // same texel read up to 128 times. It is also SHARED by the layers — there is one sky — and
            // what each layer takes of it is its own Ambient Sky Contribution.
            //
            // In SkyModel::PhysicalAtmosphere it is the marched average sky — the sky half of the same
            // 64-direction march the height fog's ambient comes out of, so a cloud's shadowed face and
            // the haze under it are lit by one atmosphere. In SkyModel::ArtisticGradient it is
            // u_ZenithRadiance, the hand-tuned dome CLD-100/101/102 calibrated the presets against,
            // which stays the gradient's ambient bit for bit (the gate is 0 there, so this branch is not
            // even taken). The GROUND term is not switched: it is the gradient's ground-bounce model in
            // both, and the sky half deliberately excludes the ground so the two do not overlap.
            vec3 ambientSky = u_ZenithRadiance.xyz;
            if (u_Atmosphere.w > 0.5f)
                ambientSky = texelFetch(u_DistantSkyLight, ivec2(SKY_DISTANT_LIGHT_SKY_TEXEL, 0), 0).rgb;

            // Where the cloud this ray sees actually IS, and how much of it there is, live in the
            // globals CloudMarchOneSegment accumulates into — see the note there for why they are
            // globals. They are initialised here, once, before the plan is walked.
            g_Transmittance   = 1.0f;
            g_Scattered       = vec3(0.0f, 0.0f, 0.0f);
            g_VisibleWeight   = 0.0f;
            g_VisibleDistance = 0.0f;
            g_LayerBudget0    = u_CloudLayers[0].MaxSteps;
            g_LayerBudget1    = u_CloudLayers[1].MaxSteps;
            g_DominantWeight  = -1.0f;
            g_DominantLayer   = plan.S0.Layer;

            // THE PLAN'S ORDER, one segment at a time. The order is the RAY'S, not the layers', which is
            // what keeps "near over far" exact in every configuration — including the one where a shell's
            // own interval contains the other's and the plan hands back three segments.
            //
            // TWO LOOPS, AND THE ONLY DIFFERENCE BETWEEN THEM IS WHERE CloudSelectLayer IS CALLED.
            //
            // That is a performance decision with numbers on it, and they are in
            // Common/CloudParams.glslh. In one line: the layer index is memory the optimiser will not
            // promote across a loop, so a segment loop that also assigns it forces the fifty-odd
            // parameter loads a density sample makes to keep a computed address and stay inside the
            // march. Selected once, before any loop, they are constant offsets again.
            //
            // So a scene with ONE layer takes the first loop, where the selection has already happened,
            // and pays for the layer machinery only in the branch it did not take. A scene with two takes
            // the second and pays the difference — which is the honest place for it, on the frame that
            // gained the second deck.
            //
            // Two LOOPS and not two inlined copies of the march inside one loop: that was tried and is
            // worse (13.64 ms against 11.78), because both copies are then live in the same loop and the
            // register allocator has to size for their sum rather than for the larger of them.
            //
            // What remains is a residue: 9.80 ms against the 8.64 this scene cost before layers existed,
            // because the shader still CONTAINS the two-layer loop even when it does not run it. Removing
            // it needs a shader permutation — one program per layer count — which is a bigger change than
            // this task, and is written up in the report rather than left as a comment.
            if (layerCount <= 1)
            {
                CloudSelectLayer(0);
                for (int segIndex = 0; segIndex < plan.Count; ++segIndex)
                {
                    if (g_Transmittance < 0.005f)
                        break;

                    CloudMarchOneSegment(CloudPlanSegment(plan, segIndex), coord, cameraPos, dir, sunDir,
                                         cosTheta, ambientSky, planetRadiusKm, geometryLimit);
                }
            }
            else
            {
                for (int segIndex = 0; segIndex < plan.Count; ++segIndex)
                {
                    if (g_Transmittance < 0.005f)
                        break;

                    CloudMarchSegment segment = CloudPlanSegment(plan, segIndex);
                    CloudSelectLayer(segment.Layer);
                    CloudMarchOneSegment(segment, coord, cameraPos, dir, sunDir, cosTheta, ambientSky,
                                         planetRadiusKm, geometryLimit);
                }
            }

            // Where the cloud this PIXEL sees is, across every layer it crossed. A ray that met nothing
            // falls back to the first shell's entry, which is the only distance it has.
            float cloudDistance = g_VisibleWeight > 1e-6f ? g_VisibleDistance / g_VisibleWeight
                                                          : CloudWorldFromKm(plan.S0.TEnter);

            // The aerial perspective is one blend over one accumulated colour, and its authored range and
            // strength are per layer — so it is the layer that contributed most of this pixel's opacity
            // that owns them.
            CloudSelectLayer(g_DominantLayer);

            // ATMOSPHERIC PERSPECTIVE: the air between the eye and the cloud. How much of it there is
            // was decided by the march (cloudDistance); WHAT it is depends on the sky model, and the two
            // answers are not two spellings of one quantity — they are two different models, one physical
            // and one authored, each correct inside its own.
            float strength = clamp(u_ShadowTint.w, 0.0f, 1.0f);

            if (u_Atmosphere.z > 0.5f)
            {
                // THE PHYSICAL ATMOSPHERE, sampled — not approximated. The froxel at the cloud's own
                // depth holds the light that stretch of air scatters toward the eye and the fraction of
                // the cloud's own light that survives it, marched from the same medium and the same LUTs
                // as the haze over the terrain below. That identity is the point: the cloud layer and the
                // ground it hangs over now dissolve into ONE integral, so there is no colour seam where
                // they meet at the horizon.
                //
                // Composed as UE composes SAMPLE_ATMOSPHERE_ON_CLOUDS (Docs/Sky/UE_SKYATMOSPHERE_RESEARCH
                // section 1.4): the cloud's radiance is attenuated by the air's transmittance, and the
                // air's own in-scatter is added over the fraction of the pixel the cloud actually covers,
                // 1 - transmittance. Where the cloud is transparent nothing is added, because the sky pass
                // already drew that pixel's full atmospheric column — this is the only arrangement in
                // which the air is neither missing behind the cloud nor counted twice beside it.
                //
                // The distance is the TRANSMITTANCE-WEIGHTED MEAN depth of the cloud, the same number the
                // fades use, and it saturates at the volume's far extent (SkyApSliceUnitFromDistance):
                // the shell reaches ~140 km and the volume typically 96, but past that the froxels have
                // long since stopped changing, and the alternative is a hard ring in the sky where the
                // volume ends.
                float distanceKm = cloudDistance * (1.0f / CLOUD_WORLD_UNITS_PER_KM) *
                                   max(u_Atmosphere.y, 0.0f);
                float sliceUnit  = SkyApSliceUnitFromDistance(distanceKm, u_Atmosphere.x);

                // Read through the exact inverse of the fill's texel-centre remap on all three axes, the
                // same three lines the fog pass reads it with.
                vec3 uvw = vec3(SkyUnitToTexelUv(uv.x, SKY_AP_VOLUME_WIDTH),
                                SkyUnitToTexelUv(uv.y, SKY_AP_VOLUME_HEIGHT),
                                SkyUnitToTexelUv(sliceUnit, SKY_AP_VOLUME_DEPTH));
                vec4 ap = texture(u_AerialPerspective, uvw);

                // The composition itself, and the Atmospheric Perspective dial over it, are
                // CloudApplyAerialPerspective — a pure function of five numbers, which is why it lives in
                // Common/CloudGeometry.glslh where the CloudMath tests drive it as C++ and pin its energy
                // bound.
                g_Scattered = CloudApplyAerialPerspective(g_Scattered, g_Transmittance, ap.rgb, ap.a, strength);
            }
            else
            {
                // THE ARTISTIC GRADIENT's own answer, frozen by teamlead decision and unchanged to the
                // bit: the gradient publishes no medium to march, so distant clouds are faded toward the
                // sky colour evaluated in this ray's direction over the authored distance range.
                SkyPacked sky;
                for (int i = 0; i < SKY_PACKED_VEC4_COUNT; ++i)
                    sky.v[i] = u_SkyPacked[i];

                // Evaluated with a sun INTENSITY of zero, deliberately. Aerial perspective is the light
                // the air in front of the cloud scatters toward the camera: the sky gradient and the
                // sun's broad halo belong in it, the solar DISC does not. Leaving the disc in would paint
                // a small blinding spot onto any distant cloud that happened to cross the sun. Zeroing
                // the intensity removes exactly the disc term (core * sunIntensity) and leaves the halo,
                // which is scaled by sunGlow instead.
                vec3 skyColour = EvaluateSky(dir, UnpackSunDirection(sky), 0.0f,
                                             UnpackSunAngularRadius(sky), UnpackSkyConfig(sky));

                float fadeStart = u_AutoDistanceFade != 0 ? CloudAutoFadeStart(u_LayerBottomAltitude)
                                                          : u_DistanceFadeStart;
                float fadeEnd   = u_AutoDistanceFade != 0
                                       ? CloudAutoFadeEnd(u_PlanetRadius, u_LayerBottomAltitude,
                                                          u_LayerThickness)
                                       : u_DistanceFadeEnd;

                float aerial = CloudRemapRange(cloudDistance, fadeStart, fadeEnd, 0.0f, 1.0f) * strength;
                g_Scattered    = mix(g_Scattered, skyColour * (1.0f - g_Transmittance), aerial);
            }

            result = vec4(g_Scattered, clamp(g_Transmittance, 0.0f, 1.0f));
            imageStore(u_CloudScatter, coord, result);
        }
    }
}
