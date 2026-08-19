#pragma once

#include <Engine/ECS/VolumetricCloudComponent.hpp>
#include <Engine/Graphic/AtmosphereEnv.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Desert::Graphic
{
    /**
     * The GPU side of VolumetricCloudData, and the ONLY place the component is turned into bytes.
     *
     * The GLSL half of this layout is the block in Editor/Resources/Shaders/Common/CloudParams.glslh,
     * member for member and in this order. The static_asserts below make a divergence a build error
     * instead of a frame in which every parameter after the inserted one is read from the wrong offset —
     * which does not look like a bug, it looks like the clouds being badly tuned.
     *
     * Units: KILOMETRES throughout. This packer is where the component's world-unit distances are
     * converted, exactly once.
     *
     * EVERY SLOT IS READ. The block is 48 floats with no padding and no reserved field. That is a deliberate
     * constraint rather than an accident of packing: a spare slot is where a future parameter gets quietly stashed
     * without a name, a range or a tooltip.
     */
    struct CloudGpuPayload
    {
        glm::vec4 Layer;        // x planet radius, y bottom altitude, z thickness, w max view distance (km)
        glm::vec4 March;        // x max steps, y stop transmittance, z tracing start (km), w extinction (1/km)
        glm::vec4 Weather;      // x coverage tile (km), y coverage, z coverage contrast, w cloud type
        glm::vec4 Detail;       // x detail tile (km), y detail strength, z density scale, w scattering albedo
        glm::vec4 Wind;         // xyz accumulated wind offset (km), w phase g
        glm::vec4 Sun;          // xyz TOWARD the sun (normalized), w light march distance (km)
        glm::vec4 SunColour;    // rgb sun irradiance (linear), w light march sample count
        glm::vec4 Ambient;      // rgb ambient radiance OR its scale, w = 1 when the distant sky light is read
        glm::vec4 MultiScatter; // x octave count, y contribution, z occlusion, w eccentricity
        glm::vec4 Aerial;       // x AP far extent (km), y AP view-distance scale, z AP gate, w type variance
        glm::vec4 Phase;        // x second lobe g, y phase blend, z AO strength, w tracing start max (km)
        glm::vec4 Fade;         // x AP start (km), y AP fade (km), z near fade end (km), w near fade start (km)
    };

    static_assert( offsetof( CloudGpuPayload, Layer ) == 0 );
    static_assert( offsetof( CloudGpuPayload, March ) == 16 );
    static_assert( offsetof( CloudGpuPayload, Weather ) == 32 );
    static_assert( offsetof( CloudGpuPayload, Detail ) == 48 );
    static_assert( offsetof( CloudGpuPayload, Wind ) == 64 );
    static_assert( offsetof( CloudGpuPayload, Sun ) == 80 );
    static_assert( offsetof( CloudGpuPayload, SunColour ) == 96 );
    static_assert( offsetof( CloudGpuPayload, Ambient ) == 112 );
    static_assert( offsetof( CloudGpuPayload, MultiScatter ) == 128 );
    static_assert( offsetof( CloudGpuPayload, Aerial ) == 144 );
    static_assert( offsetof( CloudGpuPayload, Phase ) == 160 );
    static_assert( offsetof( CloudGpuPayload, Fade ) == 176 );
    static_assert( sizeof( CloudGpuPayload ) == 192,
                   "Twelve vec4s — the shader reads exactly this and nothing more." );

    // Already a multiple of std430's 16-byte block alignment, so the buffer size IS the struct size.
    inline constexpr uint32_t kCloudPayloadBytes = sizeof( CloudGpuPayload );

    // The bindings of the cloud compute pass. SetOutput / SetStorageBuffer / SetInput take these as
    // explicit arguments and never consult the shader's own reflection, so each number here must equal
    // the one written in the shader. A mismatch lands a resource on a different descriptor rather than on
    // an error.
    inline constexpr uint32_t kCloudOutputBinding     = 0; // the RGBA16F scatter image the march writes
    inline constexpr uint32_t kCloudParamsBinding     = 1; // must equal CLOUD_PARAMS_BINDING
    inline constexpr uint32_t kCloudSceneDepthBinding = 2;
    inline constexpr uint32_t kCloudNoiseBinding      = 3;
    // The sky's DISTANT SKY LIGHT. ALWAYS bound, even in a scene with no physical atmosphere: a declared
    // sampler with no image is an invalid descriptor set, not an unused one, and this engine's compute
    // path answers an invalid set by skipping the dispatch entirely — the clouds would vanish with
    // nothing in the log. What varies is CloudGpuPayload::Ambient.w, which is 0 when there is no texel.
    inline constexpr uint32_t kCloudDistantSkyLightBinding = 4;
    // The sky's camera aerial-perspective volume, which the march composes distant cloud INTO. Bound on
    // the same terms as the texel above: always, gated by CloudGpuPayload::Aerial.z.
    inline constexpr uint32_t kCloudAerialPerspectiveBinding = 5;
    // The march's SECOND output: the depth guide the composite upsamples through (front cloud distance
    // and scene distance, kilometres). A storage image like binding 0, not a sampler, so it is bound with
    // SetOutput and lives in the same GENERAL-layout round trip as the scatter target.
    inline constexpr uint32_t kCloudGuideOutputBinding = 6;

    // The TEMPORAL RESOLVE pass (Programs/Clouds/CloudTemporalResolve.shader). Same rule as above: these
    // numbers are handed to SetInput / SetOutput / SetStorageBuffer verbatim and must equal the ones
    // written in the shader.
    inline constexpr uint32_t kCloudResolveTraceBinding        = 0; // quarter-res scatter, this frame
    inline constexpr uint32_t kCloudResolveTraceGuideBinding   = 1; // quarter-res guide, this frame
    inline constexpr uint32_t kCloudResolveHistoryBinding      = 2; // half-res scatter, previous frame
    inline constexpr uint32_t kCloudResolveHistoryGuideBinding = 3; // half-res guide, previous frame
    inline constexpr uint32_t kCloudResolveOutputBinding       = 4; // half-res reconstructed scatter
    inline constexpr uint32_t kCloudResolveGuideOutputBinding  = 5; // half-res reconstructed guide
    inline constexpr uint32_t kCloudResolveParamsBinding       = 6; // CloudResolveParams

    /**
     * Per-dispatch data: everything that changes with the CAMERA rather than with the cloud settings.
     * Rides in the push constant because ComputePipeline has no SetUniformBuffer, and a second storage
     * buffer for one matrix would be a second per-frame upload.
     */
    struct CloudPush
    {
        glm::mat4 InverseViewProjection;
        // xyz = camera position in world units; w = the frame index, which decides the dither pattern.
        // It is here rather than in the parameter block because it changes every frame while the block
        // changes only when the artist does something.
        glm::vec4 CameraPosition;
        // x, y = the sub-pixel this frame traces inside its 2x2 block of HALF-resolution pixels, each 0
        //        or 1. This is the projection jitter: the march reconstructs its ray through the HALF-res
        //        pixel (traceCoord * 2 + offset), not through the centre of its own quarter-res texel.
        // z, w = the HALF-resolution grid's size in pixels. The march writes a quarter-res image, so
        //        imageSize() cannot tell it the grid its jitter is expressed in, and half-of-half rounds
        //        UP twice — deriving it from the quarter size would be off by a pixel on odd viewports and
        //        would shear the whole jitter pattern by half a texel along the right and bottom edges.
        glm::vec4 Trace;
    };

    static_assert( sizeof( CloudPush ) == 96 );

    /**
     * Unreal's VolumetricRenderTarget sub-pixel walk, mode 0.
     *
     * Four frames cover the four half-resolution pixels of one quarter-resolution texel, in the order
     * {0, 2, 3, 1} rather than {0, 1, 2, 3}.
     *
     * WHY THAT ORDER. It is the 2x2 BAYER MATRIX read row by row — [[0, 2], [3, 1]] — so the four frames
     * visit the block in the ordered-dithering sequence rather than in raster order. Epic's own name for
     * the table says exactly this and is the only justification Epic gives: VolumetricRenderTarget.cpp:308
     * declares it as `OrderDithering2x2`, and the neighbouring branch at :314 carries
     * {0,8,2,10, 12,4,14,6, 3,11,1,9, 15,7,13,5} — the canonical 4x4 Bayer matrix — for its own
     * downsample factor. One family of tables, one rule, two sizes.
     *
     * A raster order would spend two consecutive frames on the top row before touching the bottom one,
     * which reads as a horizontal crawl at exactly the frequency the eye is best at seeing; the Bayer
     * order is the standard answer to that and spreads each successive sample as far as it can from the
     * ones already taken.
     *
     * (The comment this replaces claimed the order puts consecutive frames DIAGONALLY opposite each
     * other. It does not, and no order could: unrolled through the formula below the walk is
     * (0,0) -> (0,1) -> (1,1) -> (1,0) -> (0,0), which is the four SIDES of the square, and a diagonal
     * 4-cycle on a 2x2 block does not exist at all because the two diagonals are disjoint pairs. The
     * table was right and its stated reason was not, which is worse than no reason — the reason is what
     * the next person will decide by.)
     *
     * Pure and constexpr, so it can be evaluated where it is needed rather than cached into a member
     * that can disagree with the frame counter beside it.
     */
    struct CloudSubPixel
    {
        uint32_t X;
        uint32_t Y;
    };

    inline constexpr uint32_t kCloudTraceSubPixelCount = 4;

    inline constexpr CloudSubPixel CloudTraceSubPixel( uint32_t frameIndex )
    {
        constexpr uint32_t order[kCloudTraceSubPixelCount] = { 0u, 2u, 3u, 1u };

        const uint32_t index = order[frameIndex % kCloudTraceSubPixelCount];
        return CloudSubPixel{ index % 2u, index / 2u };
    }

    static_assert( CloudTraceSubPixel( 0u ).X == 0u && CloudTraceSubPixel( 0u ).Y == 0u );
    static_assert( CloudTraceSubPixel( 1u ).X == 0u && CloudTraceSubPixel( 1u ).Y == 1u );
    static_assert( CloudTraceSubPixel( 2u ).X == 1u && CloudTraceSubPixel( 2u ).Y == 1u );
    static_assert( CloudTraceSubPixel( 3u ).X == 1u && CloudTraceSubPixel( 3u ).Y == 0u );
    static_assert( CloudTraceSubPixel( 4u ).X == CloudTraceSubPixel( 0u ).X, "The walk must be periodic." );

    /**
     * The temporal reconstruction's per-frame data.
     *
     * A STORAGE BUFFER RATHER THAN A PUSH CONSTANT, and the reason is a hard limit rather than a
     * preference: this pass needs two 4x4 matrices (128 bytes) plus the camera and the frame's state, and
     * Vulkan only guarantees 128 bytes of push-constant space. Several desktop drivers report exactly
     * that minimum, so a 160-byte push block is a pass that works on this machine and fails elsewhere
     * with a validation error rather than a picture. The buffer is created NON-PERSISTENT, which is what
     * gives it one copy per (frame x recording renderer slot) — the Docs/RENDERER_FRAME_STATE.md rule.
     *
     * Laid out for std430 with no padding member: the vec3 leaves exactly one float of room after it and
     * HistoryValid occupies it, which is why the flag is a float and sits where it does.
     */
    struct CloudResolveParams
    {
        glm::mat4 InverseViewProjection; // clip -> world, THIS frame; rebuilds the pixel's ray
        // world -> clip, the PREVIOUS frame. Composed with the inverse above this is Unreal's
        // ClipToPrevClip; it is factored into two matrices here because the reprojected point is found by
        // walking the ray to the guide's cloud front distance, which is not a clip-space operation.
        glm::mat4  PrevViewProjection;
        glm::vec3  CameraPosition; // world units (centimetres)
        float      HistoryValid;   // 1 when the history targets hold a real previous frame, 0 otherwise
        glm::ivec2 SubPixelOffset; // this frame's traced sub-pixel, the same pair the march was given
    };

    static_assert( offsetof( CloudResolveParams, InverseViewProjection ) == 0 );
    static_assert( offsetof( CloudResolveParams, PrevViewProjection ) == 64 );
    static_assert( offsetof( CloudResolveParams, CameraPosition ) == 128 );
    static_assert( offsetof( CloudResolveParams, HistoryValid ) == 140 );
    static_assert( offsetof( CloudResolveParams, SubPixelOffset ) == 144 );
    // 152, not 160. std430 rounds a block's STRIDE up to a multiple of 16, but a stride only exists for an
    // array of blocks and this is a single one — the shader never reads past the last member, so the
    // buffer is created at exactly the struct's size and the eight bytes of tail padding are not
    // allocated. Pinned here so that inserting a member and quietly changing the size is a build error.
    static_assert( sizeof( CloudResolveParams ) == 152,
                   "The GLSL block in CloudTemporalResolve.shader reads exactly these members, in this "
                   "order, at these offsets." );

    inline constexpr uint32_t kCloudResolveParamsBytes = sizeof( CloudResolveParams );

    // One world unit is one centimetre (Common::Units), so a kilometre is 100 000 of them. Mirrored by
    // CLOUD_WORLD_UNITS_PER_KM in Common/CloudGeometry.glslh.
    inline constexpr float kCloudWorldUnitsPerKm = 100000.0f;

    /**
     * The near-camera fade's interval, in kilometres and in the order the shader consumes it.
     *
     * WHY THIS IS A PAIR AND NOT TWO NUMBERS. The march evaluates `smoothstep(start, end, t)`, and GLSL
     * leaves smoothstep UNDEFINED when start >= end: at start > end the ratio is negative before the
     * clamp on some implementations and not on others, and at start == end it is a division by zero.
     * Repairing each field on its own — which is what every other line of the packer does, and correctly
     * — cannot reach this, because both `Start = 5 km` and `End = 1 km` are individually legal, inside
     * their own sliders, and one edit apart in the Details panel. The RELATION is the thing that has to
     * be repaired, so it is repaired once, here, where the component becomes bytes.
     *
     * WHAT A CONTRADICTORY PAIR MEANS: nothing, so it is answered with OFF rather than with a guess. An
     * interval whose end does not lie past its start is not a fade over some other interval — reordering
     * the two, or nudging the end past the start, would put a distance nobody authored into a frame
     * nobody can explain, and that is the failure mode this programme has paid for most often. OFF is
     * also the shipped state: both fields default to zero, so the disabled interval is the one every
     * scene already carries.
     *
     * The shader's gate (`u_CloudFade.z > 0`) is exactly equivalent to `end > start` under this
     * guarantee, because the only pair with a zero end this returns is the pair with a zero start.
     */
    struct CloudNearFadeKm
    {
        float StartKm;
        float EndKm;
    };

    inline constexpr CloudNearFadeKm CloudResolveNearFade( float startWorld, float endWorld )
    {
        const float startKm = ( startWorld > 0.0f ? startWorld : 0.0f ) / kCloudWorldUnitsPerKm;
        const float endKm   = ( endWorld > 0.0f ? endWorld : 0.0f ) / kCloudWorldUnitsPerKm;

        if ( !( endKm > startKm ) )
            return CloudNearFadeKm{ 0.0f, 0.0f };

        return CloudNearFadeKm{ startKm, endKm };
    }

    // OFF is a fixed point: feeding the disabled pair back in leaves it disabled, so a scene that has been
    // through the packer once cannot acquire a fade by being saved and loaded.
    static_assert( CloudResolveNearFade( 0.0f, 0.0f ).EndKm == 0.0f );
    // The contradiction the review found, and the one the sliders make reachable in a single edit.
    static_assert( CloudResolveNearFade( 500000.0f, 100000.0f ).EndKm == 0.0f );
    // A degenerate interval of zero width is a division by zero in the shader, not a fade of zero length.
    static_assert( CloudResolveNearFade( 100000.0f, 100000.0f ).EndKm == 0.0f );
    // And a legal pair survives untouched, in kilometres.
    static_assert( CloudResolveNearFade( 100000.0f, 500000.0f ).StartKm == 1.0f );
    static_assert( CloudResolveNearFade( 100000.0f, 500000.0f ).EndKm == 5.0f );

    /**
     * Fill the GPU block from the component, the atmosphere and this frame's accumulated wind offset.
     * Pure: no GPU, no globals, no clock — the CloudPayload tests drive it directly.
     *
     * @param windOffsetWorld the drift accumulated by the ECS system, world units. It is a parameter
     *                        rather than a member because the accumulator belongs to the system that
     *                        owns the timestep, and a second copy here would be state that can disagree.
     *
     * THE TWO ATMOSPHERE COUPLINGS follow the sky model, gated on the same handle the height fog uses —
     * a non-null AtmosphereEnv::DistantSkyLight, published in SkyModel::PhysicalAtmosphere and nowhere
     * else. One gate rather than two, so the clouds cannot end up half physical:
     *
     *   * SUN. In the physical model the clouds are lit by SunIlluminanceOnGround — the directional
     *     light's own Color x Intensity after the atmosphere's transmittance, which is what actually
     *     falls on things. In the artistic gradient they are lit by SunIrradiance, the sky component's
     *     elevation-tinted disc brightness, because that is the quantity the gradient was authored
     *     against. The two differ by more than an order of magnitude in a typical scene and neither is a
     *     stand-in for the other.
     *
     *   * AMBIENT. In the physical model the value is a GPU texel and cannot be folded in here, so what
     *     travels is the artist's SCALE and a gate, and the shader does the multiply. In the artistic
     *     gradient the value is ZenithRadiance — the whole upper dome, which is what a cloud sees — and
     *     it is folded in here, with the gate at 0.
     *
     * WITHOUT AN ATMOSPHERE there is no sun and no sky, so both terms are zero rather than guessed. The
     * renderer does not dispatch at all in that case; the packer is written to be correct anyway, because
     * a packer that depends on its caller having checked something is a packer that will one day be
     * called by someone who did not.
     */
    inline CloudGpuPayload PackCloudParams( const ECS::VolumetricCloudData& data, const AtmosphereEnv& atmosphere,
                                            const glm::vec3& windOffsetWorld )
    {
        const bool physical = atmosphere.Valid && atmosphere.DistantSkyLight != nullptr;

        // Repaired at the boundary rather than trusted, like every payload packer: each of these is one
        // hand-edited scene file away from a division by zero or a loop that does not terminate.
        const float thicknessKm = std::max( data.LayerThickness, 1000.0f ) / kCloudWorldUnitsPerKm;
        const float bottomKm    = std::max( data.LayerBottomAltitude, 0.0f ) / kCloudWorldUnitsPerKm;
        const float planetKm    = std::max( data.PlanetRadius, 1.0f );

        glm::vec3 sunDirection{ 0.0f, 1.0f, 0.0f };
        glm::vec3 sunIrradiance{ 0.0f };
        glm::vec3 ambient{ 0.0f };

        if ( atmosphere.Valid )
        {
            sunDirection  = atmosphere.SunDirection;
            sunIrradiance = physical ? atmosphere.SunIlluminanceOnGround : atmosphere.SunIrradiance;
            ambient       = physical ? data.AmbientScale : data.AmbientScale * atmosphere.ZenithRadiance;
        }

        CloudGpuPayload p{};
        p.Layer   = glm::vec4( planetKm, bottomKm, thicknessKm,
                               std::max( data.MaxViewDistance, 0.0f ) / kCloudWorldUnitsPerKm );
        p.March   = glm::vec4( static_cast<float>( std::clamp( data.MaxSteps, 8, 512 ) ),
                               std::clamp( data.StopTransmittance, 0.0f, 1.0f ),
                               std::max( data.TracingStartDistance, 0.0f ) / kCloudWorldUnitsPerKm,
                               std::max( data.ExtinctionScale, 0.0f ) );
        p.Weather = glm::vec4( std::max( data.WeatherTileSize, 1.0f ) / kCloudWorldUnitsPerKm,
                               std::clamp( data.Coverage, 0.0f, 1.0f ), std::max( data.CoverageContrast, 0.01f ),
                               std::clamp( data.CloudType, 0.0f, 1.0f ) );
        p.Detail  = glm::vec4( std::max( data.DetailTileSize, 1.0f ) / kCloudWorldUnitsPerKm,
                               std::clamp( data.DetailStrength, 0.0f, 1.0f ), std::max( data.DensityScale, 0.0f ),
                               std::clamp( data.ScatteringAlbedo, 0.0f, 1.0f ) );
        p.Wind    = glm::vec4( windOffsetWorld / kCloudWorldUnitsPerKm, std::clamp( data.PhaseG, -0.9f, 0.9f ) );
        p.Sun     = glm::vec4( sunDirection, std::max( data.LightMarchDistance, 0.0f ) / kCloudWorldUnitsPerKm );
        p.SunColour =
             glm::vec4( sunIrradiance, static_cast<float>( std::clamp( data.LightMarchSamples, 1, 16 ) ) );
        p.Ambient      = glm::vec4( ambient, physical ? 1.0f : 0.0f );
        p.MultiScatter = glm::vec4( static_cast<float>( std::clamp( data.MultiScatterOctaves, 1, 3 ) ),
                                    std::clamp( data.MultiScatterContribution, 0.0f, 1.0f ),
                                    std::clamp( data.MultiScatterOcclusion, 0.0f, 1.0f ),
                                    std::clamp( data.MultiScatterEccentricity, 0.0f, 1.0f ) );

        // The aerial perspective is a coupling, not a dependency: without a volume the gate is 0 and the
        // shader composes the exact identity. The two scalars are read from AtmosphereEnv rather than
        // from the cloud component because they describe the VOLUME, and the volume belongs to the sky —
        // a second authored copy here is how the fill and the read end up disagreeing about the slice
        // mapping.
        p.Aerial = glm::vec4( atmosphere.AerialPerspectiveDepthKm, atmosphere.AerialPerspectiveViewDistanceScale,
                              atmosphere.AerialPerspectiveVolume != nullptr ? 1.0f : 0.0f,
                              std::clamp( data.CloudTypeVariance, 0.0f, 1.0f ) );

        // The second phase lobe, the ambient-occlusion amount and the distance past which the layer is not
        // traced at all. The last is Unreal's TracingStartMaxDistance, and its absence was found on review:
        // without it a ray that enters the shell twelve thousand kilometres away — which the geometry can
        // legitimately report before the planet test rejects it — is a ray the march would still try.
        p.Phase =
             glm::vec4( std::clamp( data.PhaseGBackward, -0.9f, 0.9f ), std::clamp( data.PhaseBlend, 0.0f, 1.0f ),
                        std::clamp( data.AmbientOcclusionStrength, 0.0f, 1.0f ),
                        std::max( data.TracingStartMaxDistance, 0.0f ) / kCloudWorldUnitsPerKm );
        // The two fades, both of which UE carries and neither of which is physics. The aerial perspective
        // is CORRECT at ninety kilometres and it correctly erases a cloud on the horizon; whether a sky is
        // wanted to look that way is an art decision, so it gets a dial rather than an argument. Zero fade
        // distance means "apply it in full from the camera", which is UE's default and the physical answer.
        //
        // The near fade is repaired as a PAIR — see CloudResolveNearFade — because its two fields feed a
        // smoothstep and GLSL leaves that undefined unless the second is strictly past the first.
        const CloudNearFadeKm nearFade =
             CloudResolveNearFade( data.NearFadeStartDistance, data.NearFadeEndDistance );

        p.Fade = glm::vec4( std::max( data.AerialPerspectiveStartDistance, 0.0f ) / kCloudWorldUnitsPerKm,
                            std::max( data.AerialPerspectiveFadeDistance, 0.0f ) / kCloudWorldUnitsPerKm,
                            nearFade.EndKm, nearFade.StartKm );
        return p;
    }

    // THERE IS NO BAKE KEY HERE ANY MORE. `CloudNoiseBakeKey` packed the component's two seeds and two
    // octave counts into a push constant for the compute bake that filled the noise volume; the volume is
    // now an asset generated offline (Engine/Assets/CloudNoiseVolume.hpp), so the pass, the push constant
    // and the four fields behind them are gone together rather than left as a struct nobody constructs.
} // namespace Desert::Graphic
