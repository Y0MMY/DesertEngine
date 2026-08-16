#pragma once

#include <Engine/ECS/VolumetricCloudsComponent.hpp>
#include <Engine/Graphic/Clouds/CloudNoiseRules.hpp>
#include <Engine/Graphic/AtmosphereEnv.hpp>
#include <Engine/Graphic/WindEnv.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

namespace Desert::Graphic
{
    /**
     * The GPU side of VolumetricCloudData, and the ONLY place the component is turned into bytes.
     *
     * The GLSL half of this layout is the block in Editor/Resources/Shaders/Common/CloudParams.glslh,
     * member for member and in this order. The static_asserts at the bottom are what make a divergence a
     * build error instead of a frame in which every parameter after the inserted one is read from the
     * wrong offset — a failure with no message, no validation error and no obvious symptom.
     *
     * Layout rule (see the glslh): every vec4 first, every 4-byte scalar after. std430 and C++ agree on
     * both alignments, so the offsets below are the same in both languages and there is no padding
     * anywhere except at the end of the struct.
     *
     * Units: WORLD UNITS (centimetres) throughout, exactly as the component authors them. The raymarch
     * converts to kilometres once, inside the shader, where the shell intersection needs it.
     */
    struct CloudGpuPayload
    {
        // ---- vec4s. Colours are LINEAR; each `.w` carries the named companion scalar on its line. ----
        glm::vec4 SunDirection;     // xyz toward sun (normalized), w = sun angular radius (radians)
        glm::vec4 SunIrradiance;    // rgb, w = SunLightIntensityScale
        glm::vec4 ZenithRadiance;   // rgb, w = AmbientSkyContribution
        glm::vec4 GroundRadiance;   // rgb, w = AmbientGroundContribution
        glm::vec4 ScatteringAlbedo; // rgb, w = AmbientHeightBias
        glm::vec4 ExtinctionTint;   // rgb, w = ExtinctionScale
        glm::vec4 SunTint;          // rgb, w = PrecipitationDarkening
        glm::vec4 ShadowTint;       // rgb, w = AtmosphericPerspective
        glm::vec4 StratusGradient;
        glm::vec4 StratocumulusGradient;
        glm::vec4 CumulusGradient;
        glm::vec4 SceneWind; // xyz = the scene's wind velocity (world units/s), w = seconds

        // ---- Cloud Layer ----
        float PlanetRadius; // from AtmosphereEnv — the cloud subsystem never owns a radius of its own
        float LayerBottomAltitude;
        float LayerThickness;
        float MaxViewDistance;
        float HorizonFadeStart;
        float HorizonFadeEnd;

        // ---- Weather ----
        float Coverage;
        float CoverageContrast;
        float WeatherTileSize;
        float WeatherWarpStrength;
        float CloudType;
        float CloudTypeVariance;
        float AnvilBias;
        float Wetness;

        // ---- Shape ----
        float ShapeTileSize;
        float BaseShapeRemapMin;
        float ShapeErosionStrength;
        float BaseGradientPower;
        float TopGradientPower;
        float DensityHeightBias;

        // ---- Detail ----
        float DetailStrength;
        float DetailTileSize;
        float DetailTypeBias;
        float BillowGradientPower;
        float BillowNoiseScale;
        float HighFreqStrength;
        float HighFreqWispSharpness;
        float HighFreqBillowSharpness;
        float HighFreqFeatureSize;
        float CurlStrength;
        float CurlTileSize;
        float DensitySharpenLow;
        float DensitySharpenHigh;
        float DensityScalePower;
        float DistanceSoftening;
        float SofteningStartDistance;
        float SofteningEndDistance;
        float NearFadeStart;
        float NearFadeEnd;
        float NearFadeMinDensity;

        // ---- Lighting ----
        float LightMarchDistance;
        float LightConeSpread;
        float PhaseForwardG;
        float PhaseBackwardG;
        float PhaseBlend;
        float SilverLiningIntensity;
        float PowderStrength;
        float PowderScale;
        float MultiScatterExtinctionFalloff;
        float MultiScatterScatterFalloff;
        float MultiScatterPhaseFalloff;
        float DistanceFadeStart;
        float DistanceFadeEnd;

        // ---- Animation ----
        float AnimationSpeed;
        float WindInfluence;
        float WindDirectionOffset;
        float ShapeScrollMultiplier;
        float DetailScrollMultiplier;
        float WeatherScrollMultiplier;
        float WindHeightShear;
        float WindUpliftSpeed;

        // ---- Quality ----
        float MinStepSize;
        float MaxStepSize;
        float StepGrowthRate;
        float CoarseStepMultiplier;
        float JitterStrength;
        float TemporalBlendFactor;
        float TemporalClampScale;

        int32_t WeatherSeed;
        int32_t WeatherOctaves;
        int32_t MaxSteps;
        int32_t EmptySamplesBeforeCoarse;
        int32_t LightMarchSamples;
        int32_t MultiScatterOctaves;

        // How much the ambient term is occluded by the cloud around and above a sample (Nubis3 pp. 141/144).
        float AmbientOcclusion;
        // 1 = derive the distance/horizon fade range from the layer geometry instead of the authored one.
        int32_t AutoDistanceFade;

        // ---- Cloud shadow map ----
        // Half-width of the sun-space shadow map in world units, and whether the march reads it at all.
        // Appended, not inserted: every offset asserted below is a promise to the shaders.
        float   CloudShadowExtent;
        int32_t CloudShadowEnabled;

        // ---- Per-cell vertical band ----
        // How far the profile map's Min/Max Height channels are allowed to move a cell's own base and
        // ceiling away from the whole layer. Appended, like everything before it: the offsets below are
        // a promise to Common/CloudParams.glslh, and an insertion silently re-reads every field after it.
        // The curves the type axis indexes are NOT here — they are baked into a texture on the CPU
        // (Graphic::BuildCloudProfileLut), so authoring a new form costs a re-upload of 3 KiB rather
        // than sixty floats crossing the bus every frame.
        float CloudHeightVariance;
    };

    // Offsets of the vec4 run, and of the first and last scalar. Spot-checking three of them would not
    // catch an insertion between two that were not checked, so the boundary of every group is asserted.
    static_assert( offsetof( CloudGpuPayload, SunDirection ) == 0 );
    static_assert( offsetof( CloudGpuPayload, SunIrradiance ) == 16 );
    static_assert( offsetof( CloudGpuPayload, ZenithRadiance ) == 32 );
    static_assert( offsetof( CloudGpuPayload, GroundRadiance ) == 48 );
    static_assert( offsetof( CloudGpuPayload, ScatteringAlbedo ) == 64 );
    static_assert( offsetof( CloudGpuPayload, ExtinctionTint ) == 80 );
    static_assert( offsetof( CloudGpuPayload, SunTint ) == 96 );
    static_assert( offsetof( CloudGpuPayload, ShadowTint ) == 112 );
    static_assert( offsetof( CloudGpuPayload, StratusGradient ) == 128 );
    static_assert( offsetof( CloudGpuPayload, StratocumulusGradient ) == 144 );
    static_assert( offsetof( CloudGpuPayload, CumulusGradient ) == 160 );
    static_assert( offsetof( CloudGpuPayload, SceneWind ) == 176 );
    static_assert( offsetof( CloudGpuPayload, PlanetRadius ) == 192, "The scalar run must start straight "
                                                                     "after the twelve vec4s — std430 "
                                                                     "pads neither side." );
    static_assert( offsetof( CloudGpuPayload, Coverage ) == 216 );
    static_assert( offsetof( CloudGpuPayload, ShapeTileSize ) == 248 );
    static_assert( offsetof( CloudGpuPayload, DetailStrength ) == 272 );
    static_assert( offsetof( CloudGpuPayload, LightMarchDistance ) == 352 );
    static_assert( offsetof( CloudGpuPayload, AnimationSpeed ) == 404 );
    static_assert( offsetof( CloudGpuPayload, MinStepSize ) == 436 );
    static_assert( offsetof( CloudGpuPayload, TemporalBlendFactor ) == 456 );
    static_assert( offsetof( CloudGpuPayload, WeatherSeed ) == 464 );
    static_assert( offsetof( CloudGpuPayload, MultiScatterOctaves ) == 484 );
    static_assert( offsetof( CloudGpuPayload, AmbientOcclusion ) == 488 );
    static_assert( offsetof( CloudGpuPayload, AutoDistanceFade ) == 492 );
    static_assert( offsetof( CloudGpuPayload, CloudShadowExtent ) == 496 );
    static_assert( offsetof( CloudGpuPayload, CloudShadowEnabled ) == 500 );
    static_assert( offsetof( CloudGpuPayload, CloudHeightVariance ) == 504 );
    static_assert( sizeof( CloudGpuPayload ) == 508,
                   "The block ends at the last float. glm's vec4 has a 4-byte alignment (no SIMD "
                   "gentypes in this build), so C++ adds no tail padding — std430 does, which is why "
                   "the buffer below is created at the rounded-up size and not at sizeof." );

    // std430 rounds a block up to its own 16-byte alignment, so the SSBO must be at least this large
    // even though the C++ struct stops short of it. Creating it at sizeof() would leave the
    // descriptor range short of the block the shader declares.
    inline constexpr uint32_t kCloudPayloadBytes = ( ( sizeof( CloudGpuPayload ) + 15u ) / 16u ) * 16u;

    // The binding the parameter buffer is bound at, in every cloud pass. Must equal
    // CLOUD_PARAMS_BINDING in Common/CloudParams.glslh — compute takes the binding as an argument and
    // never consults the shader's own reflection for it.
    inline constexpr uint32_t kCloudParamsBinding = 2;

    // The other bindings the cloud passes agree on with their shaders. Same reason, same trap.
    inline constexpr uint32_t kCloudWeatherOutputBinding = 0; // weather pass: the storage image it writes
    inline constexpr uint32_t kCloudScatterOutputBinding = 0; // raymarch: the RGBA16F scatter target
    inline constexpr uint32_t kCloudShapeNoiseBinding    = 3;
    inline constexpr uint32_t kCloudDetailNoiseBinding   = 4;
    inline constexpr uint32_t kCloudCurlNoiseBinding     = 5;
    inline constexpr uint32_t kCloudWeatherMapBinding    = 6;
    inline constexpr uint32_t kCloudSceneDepthBinding    = 7;
    inline constexpr uint32_t kCloudDepthGuideBinding    = 8; // raymarch: the RGBA8 upsampling guide
    inline constexpr uint32_t kCloudShadowMapBinding     = 9; // raymarch: the sun-space shadow map it reads
    // The second weather image and the profile table. The map tiles the sky exactly as the weather map
    // does and is written by the SAME compute pass — one dispatch, two outputs, because both fields are
    // functions of the same warped lookup and generating them apart would mean warping twice.
    inline constexpr uint32_t kCloudProfileOutputBinding = 1;  // weather pass: the second storage image
    inline constexpr uint32_t kCloudProfileMapBinding    = 10; // march/shadow: per-cell Min/Max Height
    inline constexpr uint32_t kCloudProfileLutBinding    = 11; // march/shadow: the authored type curves

    // THE PHYSICAL ATMOSPHERE, as the march reads it. Both are owned by this view's SkyboxRenderer and
    // filled earlier in the same frame (SceneRenderer's LUT slot runs before the cloud slot), and both
    // are ALWAYS bound — a declared sampler with no image is an invalid descriptor set, not an unused
    // one, exactly like the shadow-map slot above. What varies is the gate in CloudRaymarchPush.
    inline constexpr uint32_t kCloudAerialPerspectiveBinding = 12; // the 32x32x16 camera AP froxel volume
    inline constexpr uint32_t kCloudDistantSkyLightBinding   = 13; // the 1x1 average-sky texel

    // The shadow pass's own output binding. Its inputs are the same weather map and noise volumes the
    // raymarch binds, at the same numbers — one density field, one set of bindings.
    inline constexpr uint32_t kCloudShadowOutputBinding = 0;

    // The temporal resolve's own bindings. Its output is the history image it fills; its two inputs are
    // the frame the raymarch just produced and the frame this stage produced last time.
    inline constexpr uint32_t kCloudResolvedOutputBinding = 0;
    inline constexpr uint32_t kCloudCurrentFrameBinding   = 3;
    inline constexpr uint32_t kCloudHistoryBinding        = 4;

    /**
     * Per-dispatch data for the shadow-map pass: where the map is centred and how far it reaches.
     *
     * Both numbers must be the ones the RAYMARCH projects with. The extent goes through
     * CloudShadowExtentOf on both sides rather than being clamped twice, and the centre is the same
     * camera position both passes are handed in the same frame.
     */
    struct CloudShadowPush
    {
        glm::vec4 Centre; // xyz = world centre (the camera), w = half-width in world units
    };

    static_assert( sizeof( CloudShadowPush ) == 16 );

    /**
     * Per-dispatch data for the raymarch: everything that changes with the CAMERA rather than with the
     * cloud settings. It rides in the push constant because ComputePipeline has no SetUniformBuffer and
     * a second storage buffer for six numbers would be a second per-frame allocation.
     */
    struct CloudRaymarchPush
    {
        glm::mat4 InverseViewProjection;
        glm::vec4 CameraPosition; // xyz = world units, w = frame index (drives the jitter sequence)
        // x = 1 when the checkerboard is active (see CloudCheckerboardActive below): the march then
        // visits only the pixels CloudCheckerboardFresh names for this frame and leaves the rest to the
        // temporal resolve. y, z, w are unused. A vec4 and not a float because the block is std430-laid
        // out on the GLSL side and a lone float after a vec4 would disagree about the block's size.
        glm::vec4 Flags;

        // THE VIEW'S PHYSICAL ATMOSPHERE — the two facts the march needs to address the aerial-perspective
        // volume, and the two gates that say whether either physical quantity exists this frame:
        //   x = the AP volume's far extent, kilometres  (AtmosphereEnv::AerialPerspectiveDepthKm)
        //   y = the read-side view-distance scale       (AtmosphereEnv::AerialPerspectiveViewDistanceScale)
        //   z = 1 when there is an AP volume to sample, 0 otherwise
        //   w = 1 when there is a distant sky light to sample, 0 otherwise
        //
        // They ride HERE and not in the parameter block because both are per-VIEW facts about this frame
        // — which resources the sky filled — rather than authored cloud settings, the same split
        // FogPush::AerialPerspective makes for the same pair. Either gate at 0 makes its half of the
        // composition the exact arithmetic identity of the artistic-gradient path, which is what lets one
        // shader serve both sky models with no permutation.
        glm::vec4 Atmosphere;
    };

    static_assert( sizeof( CloudRaymarchPush ) == 112 );

    static_assert( sizeof( CloudRaymarchPush ) <= 128,
                   "Vulkan guarantees only 128 bytes of push-constant space, and the engine emits a "
                   "single range of exactly the size handed to SetPushConstants." );

    /**
     * Per-dispatch data for the temporal resolve. Mirrors the PushConstant block of
     * Programs/Clouds/CloudTemporalResolve.shader.
     *
     * It fills the guaranteed push-constant range EXACTLY, and that is what shapes it. The stage needs
     * two transforms — this frame's pixel-to-ray, and last frame's world-to-screen — plus the camera
     * position for the shell intersection, which is 64 + 64 + 16 = 144 bytes if both are written as
     * matrices. Only clip x, y and w are ever read out of the second one (clip z is not used by a
     * reprojection), so it travels as three rows instead of four and the block closes at 128.
     */
    struct CloudTemporalPush
    {
        // NDC to a CAMERA-RELATIVE world point: the inverse of ( projection x the view's ROTATION ), with
        // the eye translation removed before the inversion rather than subtracted after it.
        //
        // This is not a stylistic preference, it is a measured one. Inverting the absolute view-projection
        // in single precision loses direction accuracy in proportion to how far the camera sits from the
        // world origin, because the reconstruction resolves a near-plane offset of a few tens of units out
        // of coordinates of a few hundred thousand. Measured with the engine's own glm::perspective /
        // glm::lookAt at a 60 degree field of view: 1.2e-3 rad of ray error with the camera 2 km up, and
        // 1.7e-2 rad — a full degree, ten pixels of history sampled from the wrong place — with the camera
        // 30 km from the origin. Removing the translation first leaves 1e-7 rad in every case, because the
        // matrix being inverted then contains no large magnitude at all.
        glm::mat4 InverseViewProjection;

        // Rows 0, 1 and 3 of ( previousViewProjection x translate( cameraPosition ) ). Premultiplying the
        // camera translation makes the shader multiply a CAMERA-RELATIVE point: the reprojected sample can
        // sit 150 km away, and a planet-scale absolute coordinate through a projection matrix is the
        // precision trap CLD-24a exists to avoid.
        glm::vec4 PrevReprojectionRow0;
        glm::vec4 PrevReprojectionRow1;
        glm::vec4 PrevReprojectionRow3;

        // xyz = camera position in world units; w = the flag word CloudTemporalPackFlags packs: whether
        // the history image already holds a resolved frame, whether the march ran checkerboarded, and
        // the frame parity the checkerboard pattern is phased by. The flags ride here rather than in the
        // parameter block because they describe the RESOURCES' state this frame, not the artist's
        // settings — and because this block fills the guaranteed 128 bytes exactly, there is nowhere
        // else for them to ride. The GLSL decoder is CloudTemporalDecodeFlags in Common/CloudTemporal.glslh.
        glm::vec4 CameraPosition;
    };

    static_assert( sizeof( CloudTemporalPush ) == 128 );

    static_assert( sizeof( CloudTemporalPush ) <= 128,
                   "Vulkan guarantees only 128 bytes of push-constant space. If this ever has to grow, "
                   "the thing to drop is the inverse view-projection: the ray direction it reconstructs "
                   "could come from three interpolated corner rays instead. Do not drop the previous "
                   "rows — there is no cheaper form of a projection." );

    /**
     * The flag word CameraPosition.w carries to the temporal resolve, and the C++ half of the pair whose
     * GLSL half is CloudTemporalDecodeFlags in Common/CloudTemporal.glslh. Small integers survive a
     * float exactly, so the packing is lossless; the test pins that the two invert each other for every
     * combination. Only the PARITY of the frame index is packed — the checkerboard pattern has period
     * two, and parity is all the shader reads.
     */
    inline constexpr float CloudTemporalPackFlags( bool historyValid, bool checkerboard, uint32_t frameIndex )
    {
        return static_cast<float>( ( historyValid ? 1 : 0 ) | ( checkerboard ? 2 : 0 ) |
                                   ( ( frameIndex & 1u ) != 0u ? 4 : 0 ) );
    }

    /**
     * Fill the temporal push constant. Pure, and separate from the dispatch so the reprojection can be
     * driven end to end by a test: this function is where the previous view-projection becomes three
     * rows, and an error here is a cloudscape that lags or smears with no message anywhere.
     *
     * @param projection             this frame's projection matrix.
     * @param view                   this frame's view matrix. Taken apart from the projection, and not as
     *                               the product, because the eye translation has to be dropped before the
     *                               inversion — see the note on InverseViewProjection above.
     * @param previousViewProjection the product, from the frame that filled the history image.
     * @param checkerboard           true when the march visited only half the pixels this frame; the
     *                               resolve then fills the stale half from the clamped history.
     * @param frameIndex             the SAME index the raymarch push carried this frame — the two stages
     *                               must agree about which half is fresh, and this is how they do.
     */
    inline CloudTemporalPush MakeCloudTemporalPush( const glm::mat4& projection, const glm::mat4& view,
                                                    const glm::mat4& previousViewProjection,
                                                    const glm::vec3& cameraPosition, bool historyValid,
                                                    bool checkerboard, uint32_t frameIndex )
    {
        // A view matrix is rotation x translate( -eye ), so its fourth column is the entire translation.
        // Replacing it with the identity's leaves the rotation exactly — no subtraction, no cancellation.
        glm::mat4 viewRotation = view;
        viewRotation[3]        = glm::vec4( 0.0f, 0.0f, 0.0f, 1.0f );

        // previousViewProjection x translate( cameraPosition ). A translation only changes the fourth
        // column, so the product is written out directly rather than through a full 4x4 multiply by a
        // matrix that is three quarters identity. The cancellation that matters above is harmless here:
        // this column is added to terms carrying a reprojected point up to 150 km long.
        glm::mat4 relative = previousViewProjection;
        relative[3]        = previousViewProjection * glm::vec4( cameraPosition, 1.0f );

        CloudTemporalPush push{};
        push.InverseViewProjection = glm::inverse( projection * viewRotation );
        push.PrevReprojectionRow0  = glm::vec4( relative[0][0], relative[1][0], relative[2][0], relative[3][0] );
        push.PrevReprojectionRow1  = glm::vec4( relative[0][1], relative[1][1], relative[2][1], relative[3][1] );
        push.PrevReprojectionRow3  = glm::vec4( relative[0][3], relative[1][3], relative[2][3], relative[3][3] );
        push.CameraPosition =
             glm::vec4( cameraPosition, CloudTemporalPackFlags( historyValid, checkerboard, frameIndex ) );
        return push;
    }

    /**
     * The WHOLE meaning of VolumetricCloudData::TemporalMode, in one predicate.
     *
     * Off is a configuration, not a failure branch: no history images are allocated, the resolve is not
     * dispatched, and the composite magnifies the raymarch target itself — so what reaches the screen is
     * the marched image, bit for bit, and the Low tier (which authors Off) pays nothing for a stage it
     * does not use. The mode is deliberately absent from the GPU parameter block: it selects which passes
     * run, which is a decision that has to be made on this side, and a second copy on the far side of the
     * bus would be free to disagree with it.
     */
    inline constexpr bool CloudTemporalUsesHistory( ECS::CloudTemporalMode mode )
    {
        return mode == ECS::CloudTemporalMode::Reprojection;
    }

    /**
     * Whether the raymarch runs CHECKERBOARDED this frame: half the pixels marched, the other half
     * reconstructed by the temporal resolve from the reprojected, clamped history.
     *
     * Full resolution only — a documented property of the tier, not a fourteenth quality knob: at Full
     * the march is the frame budget (measured ~10x the High tier), and halving the pixels per frame is
     * the deck's own answer to that cost (Nubis3 pp. 49-50). At Half and Quarter the march is already
     * cheap and the checkerboard would double the temporal lag for nothing.
     *
     * And only when the resolve actually runs: the stale half of the target holds data the march
     * deliberately skipped, and the resolve is the stage that replaces it. Without a usable history —
     * Temporal Mode = Off, or the pair failed to allocate — the composite reads the marched image
     * directly, and a checkerboarded march would put a half-stale checkerboard on screen.
     */
    inline constexpr bool CloudCheckerboardActive( ECS::CloudResolutionScale scale, ECS::CloudTemporalMode mode,
                                                   bool historyAvailable )
    {
        return scale == ECS::CloudResolutionScale::Full && CloudTemporalUsesHistory( mode ) && historyAvailable;
    }

    /** Which low-resolution image the composite magnifies. */
    enum class CloudCompositeSource
    {
        Raymarch,        // the S2 target itself — Temporal Mode = Off, or history that could not be allocated
        TemporalHistory, // the S3 output for this frame
    };

    /**
     * @param historyReady the two history images exist AND the resolve ran this frame.
     *
     * The second argument is not defensive padding: image allocation is lazy and can fail, and CLD-34
     * requires that failure to be latched and survivable. A cloudscape with no temporal accumulation is a
     * noisier cloudscape; a composite pointed at an image that was never created is a black screen.
     */
    inline constexpr CloudCompositeSource CloudSelectCompositeSource( ECS::CloudTemporalMode mode,
                                                                      bool                   historyReady )
    {
        return CloudTemporalUsesHistory( mode ) && historyReady ? CloudCompositeSource::TemporalHistory
                                                                : CloudCompositeSource::Raymarch;
    }

    /** Pixel divisor of each resolution tier: Quarter is a quarter of the pixels per axis... */
    inline constexpr uint32_t CloudResolutionDivisor( ECS::CloudResolutionScale scale )
    {
        switch ( scale )
        {
            case ECS::CloudResolutionScale::Quarter:
                return 4;
            case ECS::CloudResolutionScale::Half:
                return 2;
            case ECS::CloudResolutionScale::Full:
                return 1;
        }
        // No default label: every enumerator is handled above, so adding a tier is a compile error here
        // rather than a silently wrong buffer size. This line is only reachable through a cast integer.
        return 1;
    }

    /** The raymarch target's dimensions for a given render-target size. Never zero: a 1x1 dispatch is
     *  meaningless but legal, whereas a zero-sized image fails to create and takes the whole pass down. */
    inline constexpr uint32_t CloudScaledExtent( uint32_t extent, ECS::CloudResolutionScale scale )
    {
        const uint32_t divisor = CloudResolutionDivisor( scale );
        return extent / divisor > 1u ? extent / divisor : 1u;
    }

    /**
     * Bytes of resolution-scaled cloud imagery a live SceneRenderer holds for a @p width x @p height view.
     *
     * Everything here is per SceneRenderer and the editor builds several of them, so this is a number
     * somebody has to be able to check — CLD-34 asks for it in the log, and the test pins it against the
     * arithmetic below rather than against a figure typed into a document:
     *
     *   scatter target      RGBA16F at the scaled size, always;
     *   depth guide         RGBA8   at the scaled size, always — the composite's bilateral upsample reads
     *                       it, and that upsample runs in both temporal modes (CLD-32a constraint 2);
     *   history x2          RGBA16F at the scaled size, only with Temporal Mode = Reprojection.
     *
     * The 512x512 weather map is NOT counted: it is one fixed megabyte that does not move with the view
     * size or with any of these settings, and folding it in would hide the term that does.
     */
    inline constexpr uint64_t CloudScaledImageBytes( uint32_t width, uint32_t height,
                                                     ECS::CloudResolutionScale scale, ECS::CloudTemporalMode mode )
    {
        const uint32_t scaledWidth  = CloudScaledExtent( width, scale );
        const uint32_t scaledHeight = CloudScaledExtent( height, scale );

        const uint64_t colour =
             Core::Formats::CalculateImageSize( scaledWidth, scaledHeight, Core::Formats::ImageFormat::RGBA16F );
        const uint64_t guide =
             Core::Formats::CalculateImageSize( scaledWidth, scaledHeight, Core::Formats::ImageFormat::RGBA8F );

        return colour + guide + ( CloudTemporalUsesHistory( mode ) ? 2u * colour : 0u );
    }

    /**
     * How fast one unit of the scene's wind Strength drives the cloudscape, in world units per second.
     *
     * WindEnv::Strength is a dimensionless "how windy is it" that grass reads as a sway amplitude, so it
     * has to be given a speed before clouds can drift by it. 60 m/s per unit of Strength is the
     * calibration: the default Strength of 0.15 becomes 9 m/s, which is a real fair-weather wind, and
     * the Storm preset's WindInfluence of 1.8 lands at 16 m/s. Clouds never author their own wind
     * direction — one wind moves the grass and the sky (SceneSettings, non-goal N6).
     */
    inline constexpr float kCloudWindSpeedPerStrength = 6000.0f; // = Common::Units::Metres( 60 )

    /**
     * Fill the GPU block from the component, the atmosphere and the scene wind. Pure: no GPU, no
     * globals, no clock — @p timeSeconds is passed in so the packing can be tested.
     *
     * Ordering invariants (fade start <= fade end, min step <= max step) are enforced HERE rather than
     * trusted: they are authored independently in the Details panel, nothing stops an artist dragging
     * End below Start, and each one is a negative range or a division by zero in the shader. Repairing
     * them at the boundary means the shader can assume them, instead of every consumer re-checking.
     */
    // The shadow map's half-width, clamped once and read by BOTH the pass that fills the map and the
    // payload the march projects with. A map narrower than the layer is thick cannot contain a single sun
    // ray through it; one wider than the far fade covers sky nothing will ever read.
    inline float CloudShadowExtentOf( const ECS::VolumetricCloudData& data )
    {
        return glm::clamp( data.CloudShadowExtent, data.LayerThickness, glm::max( data.HorizonFadeEnd, 1.0f ) );
    }

    inline CloudGpuPayload PackCloudParams( const ECS::VolumetricCloudData& data, const AtmosphereEnv& atmosphere,
                                            const WindEnv& wind, float timeSeconds )
    {
        const glm::vec3 windVelocity =
             glm::vec3( wind.Direction.x, 0.0f, wind.Direction.y ) * wind.Strength * kCloudWindSpeedPerStrength;

        CloudGpuPayload p{};

        p.SunDirection          = glm::vec4( atmosphere.SunDirection, atmosphere.SunAngularRadius );
        p.SunIrradiance         = glm::vec4( atmosphere.SunIrradiance, data.SunLightIntensityScale );
        p.ZenithRadiance        = glm::vec4( atmosphere.ZenithRadiance, data.AmbientSkyContribution );
        p.GroundRadiance        = glm::vec4( atmosphere.GroundRadiance, data.AmbientGroundContribution );
        p.ScatteringAlbedo      = glm::vec4( data.ScatteringAlbedo, data.AmbientHeightBias );
        p.ExtinctionTint        = glm::vec4( data.ExtinctionTint, data.ExtinctionScale );
        p.SunTint               = glm::vec4( data.SunTint, data.PrecipitationDarkening );
        p.ShadowTint            = glm::vec4( data.ShadowTint, data.AtmosphericPerspective );
        p.StratusGradient       = data.StratusGradient;
        p.StratocumulusGradient = data.StratocumulusGradient;
        p.CumulusGradient       = data.CumulusGradient;
        p.SceneWind             = glm::vec4( windVelocity, timeSeconds );

        p.PlanetRadius        = atmosphere.PlanetRadius;
        p.LayerBottomAltitude = data.LayerBottomAltitude;
        p.LayerThickness      = glm::max( data.LayerThickness, 1.0f );
        p.MaxViewDistance     = data.MaxViewDistance;
        p.HorizonFadeStart    = data.HorizonFadeStart;
        p.HorizonFadeEnd      = glm::max( data.HorizonFadeEnd, data.HorizonFadeStart );

        p.Coverage            = data.Coverage;
        p.CoverageContrast    = glm::max( data.CoverageContrast, 1e-3f );
        p.WeatherTileSize     = glm::max( data.WeatherTileSize, 1.0f );
        p.WeatherWarpStrength = data.WeatherWarpStrength;
        p.CloudType           = data.CloudType;
        p.CloudTypeVariance   = data.CloudTypeVariance;
        p.AnvilBias           = data.AnvilBias;
        p.Wetness             = data.Wetness;

        p.ShapeTileSize        = glm::max( data.ShapeTileSize, 1.0f );
        p.BaseShapeRemapMin    = data.BaseShapeRemapMin;
        p.ShapeErosionStrength = data.ShapeErosionStrength;
        p.BaseGradientPower    = data.BaseGradientPower;
        p.TopGradientPower     = data.TopGradientPower;
        p.DensityHeightBias    = data.DensityHeightBias;

        p.DetailStrength          = data.DetailStrength;
        p.DetailTileSize          = glm::max( data.DetailTileSize, 1.0f );
        p.DetailTypeBias          = data.DetailTypeBias;
        p.BillowGradientPower     = data.BillowGradientPower;
        p.BillowNoiseScale        = data.BillowNoiseScale;
        p.HighFreqStrength        = data.HighFreqStrength;
        p.HighFreqWispSharpness   = data.HighFreqWispSharpness;
        p.HighFreqBillowSharpness = data.HighFreqBillowSharpness;
        p.HighFreqFeatureSize     = glm::max( data.HighFreqFeatureSize, 1.0f );
        p.CurlStrength            = data.CurlStrength;
        p.CurlTileSize            = glm::max( data.CurlTileSize, 1.0f );
        p.DensitySharpenLow       = data.DensitySharpenLow;
        p.DensitySharpenHigh      = data.DensitySharpenHigh;
        p.DensityScalePower       = data.DensityScalePower;
        p.DistanceSoftening       = data.DistanceSoftening;
        p.SofteningStartDistance  = data.SofteningStartDistance;
        p.SofteningEndDistance    = glm::max( data.SofteningEndDistance, data.SofteningStartDistance );
        p.NearFadeStart           = data.NearFadeStart;
        p.NearFadeEnd             = glm::max( data.NearFadeEnd, data.NearFadeStart );
        p.NearFadeMinDensity      = data.NearFadeMinDensity;

        p.LightMarchDistance            = data.LightMarchDistance;
        p.LightConeSpread               = data.LightConeSpread;
        p.PhaseForwardG                 = data.PhaseForwardG;
        p.PhaseBackwardG                = data.PhaseBackwardG;
        p.PhaseBlend                    = data.PhaseBlend;
        p.SilverLiningIntensity         = data.SilverLiningIntensity;
        p.PowderStrength                = data.PowderStrength;
        p.PowderScale                   = data.PowderScale;
        p.MultiScatterExtinctionFalloff = data.MultiScatterExtinctionFalloff;
        p.MultiScatterScatterFalloff    = data.MultiScatterScatterFalloff;
        p.MultiScatterPhaseFalloff      = data.MultiScatterPhaseFalloff;
        p.DistanceFadeStart             = data.DistanceFadeStart;
        p.DistanceFadeEnd               = glm::max( data.DistanceFadeEnd, data.DistanceFadeStart );

        p.AnimationSpeed          = data.AnimationSpeed;
        p.WindInfluence           = data.WindInfluence;
        p.WindDirectionOffset     = data.WindDirectionOffset;
        p.ShapeScrollMultiplier   = data.ShapeScrollMultiplier;
        p.DetailScrollMultiplier  = data.DetailScrollMultiplier;
        p.WeatherScrollMultiplier = data.WeatherScrollMultiplier;
        p.WindHeightShear         = data.WindHeightShear;
        p.WindUpliftSpeed         = data.WindUpliftSpeed;

        p.MinStepSize          = glm::max( data.MinStepSize, 1.0f );
        p.MaxStepSize          = glm::max( data.MaxStepSize, p.MinStepSize );
        p.StepGrowthRate       = glm::max( data.StepGrowthRate, 0.0f );
        p.CoarseStepMultiplier = glm::max( data.CoarseStepMultiplier, 1.0f );
        p.JitterStrength       = data.JitterStrength;

        // Clamped to the range the Details panel offers, for the same reason the fade pairs are repaired
        // above: a blend factor of 0 would freeze the sky on its first frame forever, and a negative
        // clamp scale would invert the neighbourhood box into an empty one that rejects every history
        // sample. Both are one keystroke away in a scene file edited by hand.
        p.TemporalBlendFactor = glm::clamp( data.TemporalBlendFactor, 0.02f, 1.0f );
        p.TemporalClampScale  = glm::max( data.TemporalClampScale, 0.0f );

        // Folded exactly as the noise volumes fold theirs: the component authors an int with a
        // Range(0, 65535), the shader hashes a uint, and a value outside that range must land on its
        // representative inside it rather than on a different cloudscape after sign extension.
        p.WeatherSeed              = static_cast<int32_t>( CloudSeedFromComponent( data.WeatherSeed ) );
        p.WeatherOctaves           = glm::clamp( data.WeatherOctaves, 1, 8 );
        p.MaxSteps                 = glm::max( data.MaxSteps, 1 );
        p.EmptySamplesBeforeCoarse = glm::max( data.EmptySamplesBeforeCoarse, 1 );
        p.LightMarchSamples        = glm::max( data.LightMarchSamples, 1 );
        p.MultiScatterOctaves      = glm::clamp( data.MultiScatterOctaves, 1, 8 );

        p.AmbientOcclusion   = glm::clamp( data.AmbientOcclusion, 0.0f, 1.0f );
        p.AutoDistanceFade   = data.AutoDistanceFade ? 1 : 0;
        p.CloudShadowExtent  = CloudShadowExtentOf( data );
        p.CloudShadowEnabled = data.CloudShadowMap ? 1 : 0;

        // Clamped here rather than trusted, like the fade pairs above: the shader lerps the whole layer
        // toward the cell's own band by this number, and a value outside [0, 1] extrapolates the band
        // past the layer it is supposed to live inside.
        p.CloudHeightVariance = glm::clamp( data.CloudHeightVariance, 0.0f, 1.0f );

        return p;
    }
} // namespace Desert::Graphic
