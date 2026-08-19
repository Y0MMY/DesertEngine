Shader "CloudRaymarch"
{
    Compute
    {
        // The volumetric cloud march. Writes RGBA16F: .rgb = in-scattered radiance, PREMULTIPLIED, linear
        // HDR; .a = the transmittance of the cloud layer along that ray. The composite pass then needs no
        // more than `scene = rgb + scene * a`, which is the same premultiplied over-operator the height
        // fog already uses — and the same one UE writes, where the alpha channel is likewise transmittance
        // and not opacity.
        //
        // WHY COMPUTE. The march needs the scene depth to stop at geometry, and the fullscreen composite
        // draws into a framebuffer that has the depth attachment BOUND — sampling a bound attachment is a
        // feedback loop. The depth read therefore happens here, outside any render pass, with
        // ComputeImageBeginRead handling the layout round-trip. One path serves Forward and Deferred,
        // because both write this same depth attachment.
        //
        // QUARTER RESOLUTION, JITTERED. This pass writes a target a QUARTER of the framebuffer's size —
        // Unreal's VolumetricRenderTarget mode 0. Its output is not composited directly:
        // CloudTemporalResolve.shader reconstructs a HALF-resolution image from it and from that image's
        // own history, and the composite upsamples THAT.
        //
        // Each quarter-res texel stands for a 2x2 block of half-resolution pixels, and each frame traces
        // ONE of the four — u_CloudTrace.xy says which. The offset is applied to the RAY, not to the
        // store: the texel is written where it always was, but the ray it holds passes through the
        // half-res pixel the frame owns. That is Unreal's HackAddTemporalAAProjectionJitter, minus the
        // half that removes an existing TAA jitter, because this engine has none to remove.
        //
        // Four frames therefore cover every half-res pixel exactly once, and the reconstruction is what
        // turns four quarter-res traces into one half-res image instead of one blurred one.
        //
        // LINEAR HDR ONLY — no tonemap, no gamma, no exposure. The engine's tonemap owns the curve and
        // runs later in the frame.
        //
        // WHAT IS WHERE. The geometry is Common/CloudGeometry.glslh, the shape is Common/CloudField.glslh,
        // the scattering is Common/CloudLighting.glslh, and all three are compiled as C++ by their unit
        // tests from this same text. What is left in this file is ray reconstruction, the loop, and a
        // store.

        #include <Common/CloudNoise.glslh>
        #include <Common/CloudGeometry.glslh>
        #include <Common/CloudLighting.glslh>

        // For SKY_DISTANT_LIGHT_SPHERE_TEXEL only — the index of the full-sphere texel in the distant
        // sky light image. Taken from the sky's own header rather than written as a literal here, so the
        // two cannot disagree about where the value lives. SkyMedium is SkyScattering's prerequisite; the
        // integrator inside it stays inert because this pass defines neither LUT callback macro.
        #include <Common/SkyMedium.glslh>
        #include <Common/SkyScattering.glslh>

        // rgba16f: radiance is pre-tonemap HDR and transmittance is in [0,1]. Half precision carries three
        // decimal digits, an order more than an over-operator needs.
        layout(binding = 0, rgba16f) restrict writeonly uniform image2D u_CloudScatter;

        // THE DEPTH GUIDE, same size as the scatter target, and the reason the composite can upsample
        // this pass without smearing its silhouettes. Two channels, both read by CloudComposite.shader:
        //
        //   .x  CLOUD FRONT DISTANCE, km — where along this ray the first material was found. NOT the
        //       transmittance-weighted mean the aerial perspective uses below: that mean sits inside the
        //       cloud, and what separates a cloud texel from an empty one is the FIRST hit.
        //   .y  SCENE DISTANCE, km — where the ray was cut, i.e. the geometry distance under a reversed-Z
        //       depth greater than zero and the far-plane distance otherwise.
        //
        // WRITTEN ON EVERY PATH, including rays that never reach the layer. A texel the march skipped is
        // still one of the four the composite fetches, and an unwritten one carries whatever the previous
        // frame left — a stale silhouette that moves with the camera one frame late.
        //
        // .zw ARE NOT USED and are written zero. That is not a reservation: this engine's
        // Core::Formats::ImageFormat has no two-channel float format (RGBA8F / RGBA16F / RGBA32F only),
        // so a two-channel guide has to be allocated four-channel. Nothing reads .zw and nothing should
        // start reading them without a format to match.
        layout(binding = 6, rgba16f) restrict writeonly uniform image2D u_CloudGuide;

        // The scene depth attachment, presented to this dispatch by ComputeImageBeginRead and handed back
        // afterwards. Point-sampled with texelFetch, never filtered: a filtered depth across a silhouette
        // averages foreground and background into a distance where nothing is.
        Uniform(2) sampler2D u_SceneDepth;

        // The noise volume, now an ASSET rather than a bake: four named channels from the Nubis deck
        // (p.96) — R,G Curly-Alligator LF/HF, B,A Alligator LF/HF — generated on the CPU by
        // Engine::Assets::CloudNoiseVolumeGenerator from the same Common/CloudNoise.glslh this shader
        // includes, and shipped as a .dcnv file. REPEAT and LINEAR,
        // which every volume sampler in this engine is; the volume was baked to tile exactly under that
        // assumption.
        Uniform(3) sampler3D u_CloudNoise;

        // The sky's DISTANT SKY LIGHT: one texel holding the average radiance of the whole sky, marched
        // this frame by Programs/Sky/SkyDistantLight.shader. It is the physical model's ambient, and a
        // cloud is lit from every direction at once, so the full-sphere mean is the right quantity — the
        // same one the height fog reads. ALWAYS BOUND, even when it will not be read: a declared sampler
        // with no image is an INVALID descriptor set rather than an unused one, and this engine's compute
        // path answers that by silently skipping the whole dispatch — which would lose the clouds with
        // nothing in the log. u_CloudAmbient.w decides whether it is sampled.
        Uniform(4) sampler2D u_DistantSkyLight;

        // The sky's CAMERA AERIAL-PERSPECTIVE volume, 32x32x16 froxels of pre-integrated atmosphere
        // (Programs/Sky/SkyAerialPerspectiveLut.shader). Bound on the same terms as the texel above and
        // read only when u_CloudAerial.z says the volume exists.
        Uniform(5) sampler3D u_CloudAerialPerspective;

        // THE VERTICAL PROFILE TABLE, 256 x 64 RGBA32F, built on the CPU by
        // Graphic::CloudBuildProfileTable from the layer's species and uploaded by
        // VolumetricCloudRenderer whenever that species changes. U is the height fraction in the
        // envelope, V is how deep inside a placement patch the column is, .r is the profile — the same
        // arrangement as Unreal's Layout_CloudHeightProfile, whose axes are (NormAltitudeInLayer,
        // layout value).
        //
        // LINEAR AND REPEAT, like every sampler this engine creates. The wrap is why
        // CloudSampleProfileTable clamps both coordinates to the texel centres rather than to [0, 1]: a
        // height fraction of exactly 1 would otherwise wrap to the bottom row and grow the cloud's base
        // back on top of its own top.
        Uniform(7) sampler2D u_CloudProfile;

        // The seam's two callbacks. Declared here, next to the samplers, because Common/CloudField.glslh
        // must stay free of samplers to remain compilable as C++ by its tests.
        #define CLOUD_SAMPLE_NOISE(p) texture(u_CloudNoise, (p))
        #define CLOUD_SAMPLE_PROFILE(uv) texture(u_CloudProfile, (uv))

        #include <Common/CloudField.glslh>
        #include <Common/CloudParams.glslh>

        PushConstant CloudPush
        {
            mat4 u_InverseViewProjection;
            vec4 u_CameraPosition; // xyz = camera position in world units, w = frame index
            // xy = this frame's sub-pixel inside the 2x2 block of HALF-resolution pixels each quarter-res
            //      texel covers, each 0 or 1. zw = the HALF-resolution grid's size in pixels, which this
            //      pass cannot derive: imageSize() reports the quarter-res target it writes, and the two
            //      round-ups that produced it are not invertible on an odd viewport.
            vec4 u_CloudTrace;
        };

        LocalSize(8, 8, 1);

        // Optical depth from a sample toward the sun.
        //
        // The samples are placed on a SQUARED distribution — Unreal's arrangement, and the reason is
        // physical rather than aesthetic: transmittance is dominated by the material immediately around
        // the sample, so uniform spacing spends most of its samples where the answer has already
        // converged. Each segment's contribution is weighted by its own length, so the sum is a genuine
        // integral and not a biased average.
        float CloudLightOpticalDepth(CloudLayer layer, CloudFieldParams params, vec3 positionKm,
                                     vec3 toSun, float marchKm, int sampleCount, float extinctionPerKm)
        {
            float accumulated = 0.0f;
            float previous    = 0.0f;
            float invCount    = 1.0f / float(sampleCount);

            for (int i = 1; i <= sampleCount; ++i)
            {
                float normalized = float(i) * invCount;
                float current    = normalized * normalized;
                float segment    = current - previous;

                vec3 samplePos = positionKm + toSun * (marchKm * (previous + segment * 0.5f));

                // Leaving the shell ends the march. Without this the samples keep accumulating against a
                // height fraction clamped to 1, which draws a shadow from cloud that is not there.
                float radius = length(samplePos);
                if (radius > layer.TopRadiusKm || radius < layer.BottomRadiusKm)
                    break;

                float heightFraction = CloudHeightFraction(layer, samplePos);
                vec3  fieldPos = vec3(samplePos.x, length(samplePos) - layer.BottomRadiusKm, samplePos.z);

                CloudFieldSample field   = SampleCloudField(params, heightFraction, fieldPos);
                float            density = CloudSampleDensity(params, field, fieldPos);

                accumulated += density * extinctionPerKm * segment;
                previous = current;
            }

            // The weights above are fractions of the march, so the whole length scales the sum exactly
            // once, here.
            return accumulated * marchKm;
        }

        void main()
        {
            ivec2 size  = imageSize(u_CloudScatter);
            ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
            if (coord.x >= size.x || coord.y >= size.y)
                return;

            // THE PROJECTION JITTER. The ray is reconstructed through the HALF-resolution pixel this
            // frame owns — (coord * 2 + offset) — and not through the centre of the quarter-res texel the
            // result is stored in. Clamped to the last half-res pixel because the quarter grid is the half
            // grid rounded UP: on an odd half-res extent the final column's block hangs half off the
            // image, and an unclamped uv there would reach past 1 and, with this engine's REPEAT samplers,
            // fetch the opposite edge of the aerial-perspective volume.
            ivec2 halfSize  = ivec2(max(u_CloudTrace.zw, vec2(1.0f, 1.0f)));
            ivec2 halfCoord = min(coord * 2 + ivec2(u_CloudTrace.xy), halfSize - ivec2(1, 1));

            vec2 uv  = (vec2(halfCoord) + vec2(0.5f, 0.5f)) / vec2(halfSize);
            vec2 ndc = vec2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

            // The ray from the camera's own inverse view-projection, so it inherits whatever projection
            // the frame was drawn with. The engine is REVERSED-Z (Core/Projection.hpp): 1 is the near
            // plane and 0 the far one, which is why the two probe depths below are that way round and not
            // the other.
            vec4 nearH = u_InverseViewProjection * vec4(ndc.x, ndc.y, 1.0f, 1.0f);
            vec4 farH  = u_InverseViewProjection * vec4(ndc.x, ndc.y, 0.0f, 1.0f);
            vec3 nearP = nearH.xyz / max(nearH.w, 1e-9f);
            vec3 farP  = farH.xyz / max(farH.w, 1e-9f);
            vec3 rayDir = normalize(farP - nearP);

            CloudLayer layer = CloudUnpackLayer();

            // Planet-centre-relative kilometres: the frame in which both shells are centred on the origin.
            vec3 cameraKm = u_CameraPosition.xyz * (1.0f / CLOUD_WORLD_UNITS_PER_KM);
            vec3 originKm = vec3(cameraKm.x, cameraKm.y + layer.PlanetRadiusKm, cameraKm.z);

            vec2 segment = CloudLayerIntersect(layer, originKm, rayDir);

            // Geometry occludes the layer. The depth target is full resolution and the ray belongs to a
            // HALF-resolution pixel, so the depth is fetched through halfCoord and not through the
            // quarter-res coord — sampling the depth at the texel the result is STORED in rather than at
            // the one the ray was CAST through would cut this frame's ray at a neighbour's geometry, and
            // the error would swing with the jitter, which is exactly the shimmer this work removes.
            //
            // One full-res texel out of the 2x2 block stands for all four, and it is the TOP-LEFT one, not
            // the centre: `(halfCoord * depthSize) / halfSize` truncates. The two readings differ exactly
            // on a silhouette, where the choice decides whether this pixel's cloud is cut at the mountain
            // or behind it; the guide's bilateral weights hide most of the residual.
            ivec2 depthSize  = textureSize(u_SceneDepth, 0);
            ivec2 depthCoord = clamp((halfCoord * depthSize) / halfSize, ivec2(0, 0), depthSize - ivec2(1, 1));
            float deviceDepth = texelFetch(u_SceneDepth, depthCoord, 0).r;

            // The guide's scene channel, resolved for EVERY pixel and not only for the ones that have
            // geometry. A sky pixel's ray is still cut somewhere — at the far plane — and giving it that
            // real distance rather than a sentinel is what lets the composite's coherence test see an
            // unbroken sky as unbroken instead of as a discontinuity against every silhouette in it.
            float sceneKm = length(farP - u_CameraPosition.xyz) * (1.0f / CLOUD_WORLD_UNITS_PER_KM);

            // `> 0` IS THE GEOMETRY TEST under reversed-Z: a sky pixel stores 0, the far plane. Written as
            // `< 1` it would be true for every pixel including the sky, and every ray would be cut at the
            // far plane — which renders as no clouds at all, with nothing in the log.
            if (deviceDepth > 0.0f)
            {
                vec4 geomH = u_InverseViewProjection * vec4(ndc.x, ndc.y, deviceDepth, 1.0f);
                vec3 geomP = geomH.xyz / max(geomH.w, 1e-9f);
                float geomKm = length(geomP - u_CameraPosition.xyz) * (1.0f / CLOUD_WORLD_UNITS_PER_KM);
                sceneKm   = geomKm;
                segment.y = min(segment.y, geomKm);
            }

            // The authored limits: where tracing starts, and how far along the segment it may run. The
            // distance is measured FROM THE LAYER ENTRY, not from the camera — Unreal's
            // DistanceFromCloudLayerEntryPoint mode — because a limit measured from the camera would cut
            // the layer short at a fixed radius and put a visible circular edge in the sky.
            segment.x = max(segment.x, u_CloudMarch.z);
            segment.y = min(segment.y, segment.x + max(u_CloudLayer.w, 0.0f));

            // A ray whose entry is beyond the cutoff is not traced at all. It bounds the cost of the
            // grazing rays that cost the most and show the least, and it is the second line of defence
            // behind the planet test: the geometry can legitimately report an entry thousands of
            // kilometres away, and nothing else in this loop would decline to march it.
            if (segment.x > max(u_CloudPhase.w, 0.0f))
            {
                imageStore(u_CloudScatter, coord, vec4(0.0f, 0.0f, 0.0f, 1.0f));
                imageStore(u_CloudGuide, coord, vec4(sceneKm, sceneKm, 0.0f, 0.0f));
                return;
            }

            if (segment.y <= segment.x)
            {
                imageStore(u_CloudScatter, coord, vec4(0.0f, 0.0f, 0.0f, 1.0f));

                // This ray never entered the layer, so the end of its search IS the end of the ray. Both
                // guide channels therefore carry the same distance, which is what makes a whole region of
                // cloudless sky perfectly coherent to the composite and sends it down the cheap bilinear
                // path — a filter that fires everywhere quantizes the frame onto this pass's own grid.
                imageStore(u_CloudGuide, coord, vec4(sceneKm, sceneKm, 0.0f, 0.0f));
                return;
            }

            float length_km = segment.y - segment.x;
            float stepCount = CloudStepCount(length_km, CLOUD_MIN_STEPS, max(u_CloudMarch.x, CLOUD_MIN_STEPS),
                                             CLOUD_DISTANCE_TO_MAX_STEPS_KM);
            int   stepTotal = int(stepCount);

            // TWO STEP SIZES, and the whole point is that they are spent in different places. The fine
            // step is the finest the march ever goes; the coarse one is a multiple of it and is used only
            // while the ray is outside cloud, where the answer is exactly zero and any step size gives it
            // correctly.
            //
            // The budget therefore stops being a resolution and becomes a resolution WHERE IT MATTERS: a
            // ray that crosses mostly empty sky finishes in a fraction of the iterations, and a ray inside
            // cloud gets to spend all of them on the part that has something in it.
            //
            // The multiplier and its partner constant live in Common/CloudGeometry.glslh, together and
            // next to the relation they have to satisfy — they are two numbers that must agree, and here
            // they were two literals a hundred lines apart in a file no test can compile.
            float stepKm       = length_km / stepCount;
            float coarseStepKm = CloudCoarseStepKm(stepKm);

            // The start offset inside the first step, so the sample planes of neighbouring pixels do not
            // line up. Without it the uniform schedule draws the layer as a set of concentric shells —
            // the banding is the step planes themselves, seen edge-on.
            //
            // The hash includes the frame index, so the pattern moves and the eye integrates it. That is
            // the whole reason a still frame of this pass is NOT a valid check of it: a fixed camera and a
            // fixed frame index show one realization of the dither, not what the viewer sees.
            // Hashed on the HALF-res pixel, not on the quarter-res texel: two frames of the same block
            // trace different half-res pixels, so hashing halfCoord gives them different start offsets as
            // well as different rays. Hashing coord would give the whole 2x2 block one offset per frame,
            // and the temporal reconstruction would then average four samples that share their bias.
            uint  jitterHash = CloudHashCell(uint(halfCoord.x), uint(halfCoord.y),
                                             uint(u_CameraPosition.w), 0x51ED270Bu);
            float jitter     = float(jitterHash & 0xFFFFu) * (1.0f / 65536.0f);

            CloudFieldParams params = CloudUnpackFieldParams();

            vec3  toSun        = normalize(u_CloudSun.xyz);
            float phase        = CloudPhaseDualLobe(dot(rayDir, toSun), u_CloudWind.w,
                                                    u_CloudPhase.x, u_CloudPhase.y);
            float extinction   = max(u_CloudMarch.w, 0.0f);
            float albedo       = clamp(u_CloudDetail.w, 0.0f, 1.0f);
            float lightMarchKm = max(u_CloudSun.w, 0.0f);
            int   lightSamples = int(clamp(u_CloudSunColour.w, 1.0f, 16.0f));
            float stopT        = clamp(u_CloudMarch.y, 0.0f, 1.0f);
            int   octaveCount  = int(clamp(u_CloudMultiScatter.x, 1.0f, 3.0f));

            // The ambient a cloud sits in, resolved once per pixel rather than per step: in the physical
            // model it is the marched full-sphere mean scaled by the artist's factor, in the artistic
            // gradient it is the dome value the packer already folded down. Neither model is a stand-in
            // for the other, which is why the choice is made from a gate rather than from whichever
            // happens to be non-zero.
            vec3 ambientRadiance = u_CloudAmbient.rgb;
            if (u_CloudAmbient.w > 0.5f)
            {
                ambientRadiance = u_CloudAmbient.rgb *
                                  texelFetch(u_DistantSkyLight, ivec2(SKY_DISTANT_LIGHT_SPHERE_TEXEL, 0), 0).rgb;
            }

            vec3  luminance     = vec3(0.0f, 0.0f, 0.0f);
            float transmittance = 1.0f;
            float t             = segment.x + jitter * stepKm;

            // Where along the ray this pixel's cloud effectively IS, weighted by how much of the ray's
            // light each sample still gets to contribute. A cloud is not at one distance, but the
            // atmosphere in front of it has to be evaluated at one — and the transmittance-weighted mean
            // is the distance at which a single evaluation is closest to the integral. Weighting by depth
            // instead would put the answer inside the far side of a thick cloud, where nothing visible
            // happens.
            float aerialWeightedT = 0.0f;
            float aerialWeightSum = 0.0f;

            // The guide's cloud channel: the distance of the FIRST sample that had any material in it.
            // Negative means "not found yet" — a distance can never be negative here, so the sentinel
            // cannot collide with an answer. It is resolved to a real distance after the loop, on every
            // path, because the composite compares this number across four texels and a sentinel that
            // leaked out of one of them would read as a silhouette where there is none.
            float cloudFrontKm = -1.0f;

            // The two-tier state. `emptyRun` counts CONSECUTIVE fine samples that found no profile; after
            // enough of them the march decides it has left the cloud and returns to coarse steps.
            bool fine     = false;
            int  emptyRun = 0;

            // BOTH TIERS JUDGE BY THE SAME QUANTITY — the un-eroded profile. This is not a detail. If the
            // coarse tier tested the profile and the fine tier tested the eroded density, then everywhere
            // the profile is positive and the erosion has cut it to zero — which is most of a procedural
            // cloudscape — the machine would drop to fine, find nothing, walk back to coarse, and land in
            // the same place. Net advance per cycle would be zero and the march would stand still while
            // burning its whole budget. CloudTwoTierCycleAdvanceKm is that net advance, and
            // Desert/Tests/Engine/CloudGeometry asserts it stays positive.
            const int kEmptyFineSamplesBeforeCoarse = CLOUD_EMPTY_FINE_SAMPLES_BEFORE_COARSE;

            for (int i = 0; i < stepTotal; ++i)
            {
                if (t >= segment.y)
                    break;

                vec3  samplePos      = originKm + rayDir * t;
                float heightFraction = CloudHeightFraction(layer, samplePos);

                // The field is handed the ALTITUDE above the layer's base, never the planet-relative
                // height. Two reasons, and the second is the one that bites: the noise is periodic, so a
                // y of 6363 kilometres wraps to something arbitrary rather than to something meaningful;
                // and at that magnitude float32 resolves 0.4 metres, which quantizes the vertical
                // structure of a two-kilometre layer onto a visible ladder.
                vec3 fieldPos = vec3(samplePos.x, length(samplePos) - layer.BottomRadiusKm, samplePos.z);

                CloudFieldSample field = SampleCloudField(params, heightFraction, fieldPos);

                if (!fine)
                {
                    // Outside cloud: keep striding. On the first hit, step BACK one coarse step and drop
                    // to fine, so the material between the last coarse sample and this one is not skipped
                    // — that gap is a whole coarse step of cloud and its absence reads as a flat face.
                    if (field.Profile > 0.0f)
                    {
                        fine     = true;
                        emptyRun = 0;
                        t        = max(segment.x, t - coarseStepKm);
                        continue;
                    }

                    t += coarseStepKm;
                    continue;
                }

                if (field.Profile <= 0.0f)
                {
                    ++emptyRun;
                    if (emptyRun >= kEmptyFineSamplesBeforeCoarse)
                    {
                        fine     = false;
                        emptyRun = 0;
                    }
                    t += stepKm;
                    continue;
                }

                emptyRun = 0;

                {
                    float density = CloudSampleDensity(params, field, fieldPos);

                    // The near-camera fade. A camera inside the layer otherwise meets full density at
                    // arm's length, which fills the screen with a flat wall; UE fades the nearest metres
                    // for exactly this. Both distances at zero leave the density untouched.
                    if (u_CloudFade.z > 0.0f)
                        density *= smoothstep(u_CloudFade.w, u_CloudFade.z, t);

                    if (density > 0.0f)
                    {
                        // Recorded at the first hit and never again: the front of the cloud, not its
                        // middle. Placed before the lighting so an early break on transmittance still
                        // leaves the guide with the distance the ray actually met material at.
                        if (cloudFrontKm < 0.0f)
                            cloudFrontKm = t;

                        float sigmaT = density * extinction;

                        // ONE shadow ray serves every scattering order. That is the whole economy of the
                        // octave approximation: the expensive part is finding how much material lies
                        // between this sample and the sun, and each order then reuses that number with its
                        // own extinction scale.
                        float opticalDepth = CloudLightOpticalDepth(layer, params, samplePos, toSun,
                                                                    lightMarchKm, lightSamples, extinction);

                        // Authored, not fixed at full: UE carries the amount in the alpha of its albedo
                        // parameter, so it IS a dial there and the earlier note here was wrong.
                        float ambientOcclusion = CloudAmbientOcclusion(field.Profile, u_CloudPhase.z);

                        // Wrenninge's multiple-scattering octaves, as Unreal implements them. Each order
                        // scatters less, is absorbed less and is less directional than the one before it,
                        // so the series falls away super-exponentially and three orders are enough.
                        // Without this a cloud is lit by single scattering alone, which is physically grey:
                        // what makes a real cloud white is light that has bounced inside it many times.
                        //
                        // THE THREE FACTORS ARE NOT BUILT THE SAME WAY, and the difference is Unreal's
                        // rather than an inconsistency of ours. Scattering and extinction ACCUMULATE —
                        // each octave multiplies the previous octave's coefficient by the current step,
                        // and only then is the step squared, giving 1, c, c^3 (VolumetricCloud.usf:388-393).
                        // The phase does NOT accumulate: every octave lerps from the BASE phase toward
                        // isotropic, and it is only the lerp factor that is squared, giving 1, p, p^2
                        // (VolumetricCloud.usf:422-429). Carrying the accumulating scheme over to the phase
                        // — which is what this loop used to do — gives the third octave p^3 instead of p^2,
                        // and at the shipped eccentricity of 0.18 that is a weight of 0.0058 against
                        // 0.0324: six times closer to isotropic than the reference, which shows up as a
                        // cloud whose silver lining is missing from its third order.
                        float scatterFactor = 1.0f;
                        float extinctFactor = 1.0f;
                        float scatterStep   = clamp(u_CloudMultiScatter.y, 0.0f, 1.0f);
                        float extinctStep   = clamp(u_CloudMultiScatter.z, 0.0f, 1.0f);

                        // UE's MsPhaseFactor. It starts at the authored step and is squared once per
                        // octave, and the octave USES it directly rather than a running product of it. The
                        // first octave is the base phase itself, which a factor of 1 expresses exactly.
                        float phaseStep   = clamp(u_CloudMultiScatter.w, 0.0f, 1.0f);
                        float phaseFactor = 1.0f;

                        for (int octave = 0; octave < octaveCount; ++octave)
                        {
                            if (octave > 0)
                            {
                                scatterFactor *= scatterStep;
                                extinctFactor *= extinctStep;

                                scatterStep *= scatterStep;
                                extinctStep *= extinctStep;

                                phaseFactor = phaseStep;
                                phaseStep *= phaseStep;
                            }

                            float octavePhase   = mix(CLOUD_ISOTROPIC_PHASE, phase, phaseFactor);
                            float sunVisibility = exp(-opticalDepth * extinctFactor);

                            vec3 inScatter = u_CloudSunColour.rgb * (sunVisibility * octavePhase);

                            // The sky's ambient belongs to the FIRST order only. Unreal leaves it out of
                            // the octaves deliberately: the approximation carries no occlusion of its own,
                            // so feeding an unoccluded ambient into every order flattens the cloud into a
                            // uniform glow.
                            if (octave == 0)
                                inScatter += ambientRadiance * ambientOcclusion;

                            float octaveExtinction = sigmaT * extinctFactor;
                            vec3  scattering       = inScatter * (albedo * sigmaT * scatterFactor);

                            luminance += transmittance *
                                         CloudScatterIntegral(scattering, octaveExtinction, stepKm);
                        }

                        // Recorded BEFORE the ray is attenuated: the weight is how much this sample was
                        // able to contribute, not how much is left after it.
                        aerialWeightedT += t * transmittance;
                        aerialWeightSum += transmittance;

                        // The view ray is attenuated by the medium itself, once — the octaves are orders
                        // of scattering INTO this ray, not extra material along it.
                        transmittance *= CloudBeerTransmittance(sigmaT, stepKm);

                        if (transmittance < stopT)
                            break;
                    }
                }

                t += stepKm;
            }

            // AERIAL PERSPECTIVE. Ninety kilometres of air between the camera and a distant cloud is not
            // nothing: it scatters its own light in and attenuates what comes back, which is why a real
            // cloud on the horizon is the colour of the sky and not the colour of a cloud. Without this
            // term the layer ends at the horizon as an opaque white wall, and no amount of coverage,
            // density or fade fixes it — the wall is the absence of the atmosphere, not the presence of
            // too much cloud.
            //
            // One fetch per pixel at the weighted distance above, composed exactly as Unreal does:
            // the volume's in-scattering scaled by how much cloud this pixel actually has, plus the
            // cloud's own radiance behind the volume's transmittance.
            if (u_CloudAerial.z > 0.5f && aerialWeightSum > 0.0f)
            {
                float meanDistanceKm = (aerialWeightedT / aerialWeightSum) * max(u_CloudAerial.y, 0.0f);
                float sliceUnit      = SkyApSliceUnitFromDistance(meanDistanceKm, u_CloudAerial.x);

                // Read through the exact inverse of the fill's texel-centre remap on all three axes, so
                // the frame's edges and the volume's near plane land on written texels rather than
                // halfway between them.
                vec3 uvw = vec3(SkyUnitToTexelUv(uv.x, SKY_AP_VOLUME_WIDTH),
                                SkyUnitToTexelUv(uv.y, SKY_AP_VOLUME_HEIGHT),
                                SkyUnitToTexelUv(sliceUnit, SKY_AP_VOLUME_DEPTH));

                vec4  aerial        = texture(u_CloudAerialPerspective, uvw);
                float cloudCoverage = 1.0f - clamp(transmittance, 0.0f, 1.0f);

                // HOW MUCH of the atmosphere to apply. Full is the physical answer and UE's default, and
                // it erases a cloud on the horizon because ninety kilometres of air genuinely does. A sky
                // that wants its distant band back ramps the haze in from a distance instead; the dial
                // exists in UE for the same reason and is off by default here for the same reason.
                float aerialAmount = 1.0f;
                if (u_CloudFade.y > 0.0f)
                    aerialAmount = clamp((meanDistanceKm - u_CloudFade.x) / u_CloudFade.y, 0.0f, 1.0f);

                vec3 hazed = aerial.rgb * cloudCoverage + aerial.a * luminance;
                luminance  = mix(luminance, hazed, aerialAmount);
            }

            imageStore(u_CloudScatter, coord, vec4(luminance, clamp(transmittance, 0.0f, 1.0f)));

            // A ray that marched the whole segment and found nothing reports THE END OF ITS OWN SEARCH.
            // That is a real distance rather than a magic number, and it is the one that makes the
            // bilateral weight behave: an empty neighbour of a cloud at 3 km reports the far side of the
            // shell, several kilometres away, so the weight `1 / (dKm * 1000 + 1)` rejects it outright.
            // A large sentinel would do the same job here but not at the horizon, where a whole
            // neighbourhood of empty rays must agree with each other exactly enough to stay coherent —
            // and half precision has no room for a sentinel that is both huge and finite.
            imageStore(u_CloudGuide, coord,
                       vec4(cloudFrontKm < 0.0f ? segment.y : cloudFrontKm, sceneKm, 0.0f, 0.0f));
        }
    }
}
