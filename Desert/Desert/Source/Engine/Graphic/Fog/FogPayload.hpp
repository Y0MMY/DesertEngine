#pragma once

#include <Engine/ECS/ExponentialHeightFogComponent.hpp>
#include <Engine/Graphic/AtmosphereEnv.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

namespace Desert::Graphic
{
    /**
     * The GPU side of ExponentialHeightFogData, and the ONLY place the component is turned into bytes.
     *
     * The GLSL half of this layout is the block in Editor/Resources/Shaders/Common/FogParams.glslh,
     * member for member and in this order. The static_asserts below make a divergence a build error
     * instead of a frame in which every parameter after the inserted one is read from the wrong offset —
     * the CloudPayload.hpp arrangement, verbatim.
     *
     * Units: KILOMETRES and per-kilometre coefficients throughout — this packer is where the component's
     * world-unit distances and UE-unit densities are converted, exactly once ("kilometres once, inside",
     * the cloud payload's rule; the closed form's precision argument is in HeightFog.glslh).
     */
    struct FogGpuPayload
    {
        glm::vec4 SunDirection; // xyz toward sun (normalized), w = DirectionalInscatteringExponent
        glm::vec4 Directional;  // rgb lobe colour, w = directional start distance (km)
        glm::vec4 Inscattering; // rgb fog colour (sky ambient folded in), w = 1 - FogMaxOpacity
        glm::vec4 Layer0;       // x density /km, y falloff /km, z fog height (km), w start distance (km)
        glm::vec4 Layer1;       // x density /km, y falloff /km, z fog height (km), w cutoff distance (km)
    };

    static_assert( offsetof( FogGpuPayload, SunDirection ) == 0 );
    static_assert( offsetof( FogGpuPayload, Directional ) == 16 );
    static_assert( offsetof( FogGpuPayload, Inscattering ) == 32 );
    static_assert( offsetof( FogGpuPayload, Layer0 ) == 48 );
    static_assert( offsetof( FogGpuPayload, Layer1 ) == 64 );
    static_assert( sizeof( FogGpuPayload ) == 80, "Five vec4s, nothing else — the shader reads exactly this." );

    // Already a multiple of std430's 16-byte block alignment, so the buffer size IS the struct size.
    inline constexpr uint32_t kFogPayloadBytes = sizeof( FogGpuPayload );

    // The bindings of the fog compute pass. SetStorageBuffer / SetInput take these as explicit
    // arguments and never consult the shader's own reflection — kFogParamsBinding must equal
    // FOG_PARAMS_BINDING in Common/FogParams.glslh (the SkyPayload.hpp binding-number trap).
    inline constexpr uint32_t kFogOutputBinding     = 0; // the RGBA16F fog image the pass writes
    inline constexpr uint32_t kFogParamsBinding     = 1;
    inline constexpr uint32_t kFogSceneDepthBinding = 2;
    // The sky's camera aerial-perspective volume, which this pass composes ITSELF OVER. ALWAYS bound,
    // even in a scene with no atmosphere at all: a declared sampler with no image is an invalid
    // descriptor set, not an unused one (the cloud raymarch's shadow-map slot, same rule). What varies
    // is FogPush::AerialPerspective.z, which is 0 when there is no volume — and then the shader never
    // reads the binding and composes the exact identity instead.
    inline constexpr uint32_t kFogAerialPerspectiveBinding = 3;

    /**
     * Per-dispatch data: everything that changes with the CAMERA rather than with the fog settings.
     * Rides in the push constant for the same reason CloudRaymarchPush does — ComputePipeline has no
     * SetUniformBuffer, and a second storage buffer for one matrix would be a second per-frame upload.
     */
    struct FogPush
    {
        glm::mat4 InverseViewProjection;
        glm::vec4 CameraPosition; // xyz = world units, w unused (vec4 keeps the std430 block unambiguous)

        // The aerial perspective this pass composes itself over, and the two switches that decide what
        // the dispatch actually evaluates. Both gates are here rather than in the fog parameter block
        // because both are per-VIEW facts about this frame, not authored fog:
        //   x = the AP volume's far extent, kilometres  (AtmosphereEnv::AerialPerspectiveDepthKm)
        //   y = the read-side view-distance scale       (AtmosphereEnv::AerialPerspectiveViewDistanceScale)
        //   z = 1 when there is an AP volume to sample, 0 otherwise
        //   w = 1 when the height fog itself is enabled, 0 otherwise
        // Either gate at 0 makes its half of the composite the exact arithmetic identity, which is what
        // lets one pass serve fog-without-sky, sky-without-fog and both, with no permutation.
        glm::vec4 AerialPerspective;
    };

    static_assert( sizeof( FogPush ) == 96 );

    // One world unit is one centimetre (Common::Units), so a kilometre is 100 000 of them.
    inline constexpr float kFogWorldUnitsPerKm = 100000.0f;

    // UE authors FogDensity and FogHeightFalloff "per 1000 cm" (its scene proxy divides both by 1000
    // before they reach the shader — research doc section 3.2). Per kilometre that is x100000 / 1000.
    inline constexpr float kFogUEUnitsPerKm = 100.0f;

    /**
     * Fill the GPU block from the component, the atmosphere and the fog entity's height. Pure: no GPU,
     * no globals, no clock — the HeightFog tests drive it directly.
     *
     * @param fogHeightWorldY the fog entity's TransformComponent Y, world units — the layer's floor,
     *                        never a field of the component (one owner, like UE's component transform).
     *
     * The two atmosphere couplings, and their honest status:
     *   * DIRECTIONAL: UE feeds the lobe the sun's POST-TRANSMITTANCE illuminance
     *     (AtmosphereLightIlluminanceOnGroundPostTransmittance) scaled by the sky's
     *     HeightFogContribution. Until sky Phase 4 lands, AtmosphereEnv::SunIrradiance — the same
     *     elevation-tinted sun the clouds are lit by — is that quantity's stand-in, at contribution 1.
     *   * AMBIENT: UE adds the Distant-Sky-Light LUT's average sky. Our stand-in until Phase 4 is the
     *     mean of AtmosphereEnv's dome and ground-bounce radiance — the same pair the clouds' ambient
     *     reads, so fog and clouds disagree about the sky by construction of neither.
     */
    inline FogGpuPayload PackFogParams( const ECS::ExponentialHeightFogData& data, const AtmosphereEnv& atmosphere,
                                        float fogHeightWorldY )
    {
        const float heightKm = fogHeightWorldY / kFogWorldUnitsPerKm;

        // Repaired at the boundary rather than trusted, like every payload packer: each of these is one
        // hand-edited scene file away from a negative density or a zero-degree pow lobe.
        const float density0 = glm::max( data.FogDensity, 0.0f ) * kFogUEUnitsPerKm;
        const float falloff0 = glm::max( data.FogHeightFalloff, 0.0f ) * kFogUEUnitsPerKm;
        const float density1 = glm::max( data.SecondFogDensity, 0.0f ) * kFogUEUnitsPerKm;
        const float falloff1 = glm::max( data.SecondFogHeightFalloff, 0.0f ) * kFogUEUnitsPerKm;

        const float maxOpacity = glm::clamp( data.FogMaxOpacity, 0.0f, 1.0f );

        glm::vec3 ambient{ 0.0f };
        glm::vec3 sunLobe = data.DirectionalInscatteringLuminance;
        glm::vec3 sunDir{ 0.0f, 1.0f, 0.0f };
        if ( atmosphere.Valid )
        {
            ambient = data.SkyAtmosphereAmbientContributionColorScale * 0.5f *
                      ( atmosphere.ZenithRadiance + atmosphere.GroundRadiance );
            sunLobe += atmosphere.SunIrradiance;
            sunDir = atmosphere.SunDirection;
        }
        else
        {
            // Without an atmosphere there is no sun to build the lobe around: an authored directional
            // colour pointed at an invented direction would be a glow with no light behind it, so the
            // whole term is dropped rather than guessed.
            sunLobe = glm::vec3( 0.0f );
        }

        FogGpuPayload p{};
        p.SunDirection = glm::vec4( sunDir, glm::max( data.DirectionalInscatteringExponent, 1.0f ) );
        p.Directional  = glm::vec4( sunLobe, glm::max( data.DirectionalInscatteringStartDistance, 0.0f ) /
                                                  kFogWorldUnitsPerKm );
        p.Inscattering = glm::vec4( data.FogInscatteringLuminance + ambient, 1.0f - maxOpacity );
        p.Layer0 =
             glm::vec4( density0, falloff0, heightKm, glm::max( data.StartDistance, 0.0f ) / kFogWorldUnitsPerKm );
        p.Layer1 = glm::vec4( density1, falloff1, heightKm + data.SecondFogHeightOffset / kFogWorldUnitsPerKm,
                              glm::max( data.FogCutoffDistance, 0.0f ) / kFogWorldUnitsPerKm );
        return p;
    }
} // namespace Desert::Graphic
