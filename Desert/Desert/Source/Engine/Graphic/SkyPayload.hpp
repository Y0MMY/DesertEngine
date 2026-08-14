#pragma once

#include <Engine/Graphic/SkySettings.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

namespace Desert::Graphic
{
    // THE sky parameter block, byte for byte. One layout serves three consumers: the sky graphics pass, the
    // IBL bake compute dispatch, and (through the same buffer) the volumetric cloud compute pass.
    //
    // The GLSL side of this layout is `SkyPacked` in Editor/Resources/Shaders/Common/Atmosphere.glslh, and
    // no shader reads a field of it directly — they call the unpack helpers there. That is the point: the
    // layout below can be reordered or extended without any consumer changing a line, as long as the two
    // sides move together. SKY_PACKED_VEC4_COUNT there and kSkyPackedVec4Count here are the tie, and the
    // static_asserts at the bottom make a mismatch a build error instead of a corrupted frame.
    //
    // std430 vs std140 does not matter for this block: it is an array of vec4, and both layouts agree on a
    // 16-byte stride for that.
    //
    // The block GREW from 7 to 13 vec4s when the physical-atmosphere medium landed (vec4s 7-12, read by
    // the transmittance / multi-scattering LUT passes). Extension is APPEND-ONLY: every existing offset is
    // a promise to every shader that reads the block.
    inline constexpr uint32_t kSkyPackedVec4Count = 13;

    struct SkyGpuPayload
    {
        glm::vec4 SunDirection; // 0   xyz = direction TOWARD the sun (normalized), w = sun intensity
        glm::vec4 Zenith;       // 16  rgb = zenith colour,  w = sky brightness
        glm::vec4 Horizon;      // 32  rgb = horizon colour, w = horizon falloff
        glm::vec4 Sun;          // 48  rgb = sun colour,     w = sun glow
        glm::vec4 Sunset;       // 64  rgb = sunset colour,  w = sunset intensity
        glm::vec4 Ground;       // 80  rgb = ground colour,  w = star intensity
        glm::vec4 Night;        // 96  rgb = night colour,   w = sun angular RADIUS in RADIANS

        // ---- Physical atmosphere medium (SkyModel::PhysicalAtmosphere). Coefficients are PER
        // KILOMETRE (the authored unit), altitudes and scale heights in kilometres; the one world-unit
        // quantity (the planet radius) is converted to km once, inside the shader — the same rule the
        // cloud payload states at CloudPayload.hpp. ----
        glm::vec4 MediumRayleigh;      // 112  rgb = Rayleigh scattering /km, w = Rayleigh scale height (km)
        glm::vec4 MediumMie;           // 128  rgb = Mie scattering /km,      w = Mie scale height (km)
        glm::vec4 MediumMieAbsorption; // 144  rgb = Mie absorption /km,      w = Mie anisotropy g
        glm::vec4 MediumOzone;         // 160  rgb = ozone absorption /km,    w = atmosphere height (km)
        glm::vec4 MediumGround;        // 176  rgb = ground albedo,           w = multi-scattering factor
        glm::vec4 MediumTentPlanet;    // 192  x = ozone tip altitude (km), y = ozone tip value,
                                       //      z = ozone tent width (km),  w = planet radius (WORLD UNITS)
    };

    static_assert( sizeof( SkyGpuPayload ) == kSkyPackedVec4Count * sizeof( glm::vec4 ),
                   "SkyGpuPayload must be exactly SKY_PACKED_VEC4_COUNT vec4s — the shader reads it as one." );
    static_assert( offsetof( SkyGpuPayload, SunDirection ) == 0 );
    static_assert( offsetof( SkyGpuPayload, Zenith ) == 16 );
    static_assert( offsetof( SkyGpuPayload, Horizon ) == 32 );
    static_assert( offsetof( SkyGpuPayload, Sun ) == 48 );
    static_assert( offsetof( SkyGpuPayload, Sunset ) == 64 );
    static_assert( offsetof( SkyGpuPayload, Ground ) == 80 );
    static_assert( offsetof( SkyGpuPayload, Night ) == 96 );
    static_assert( offsetof( SkyGpuPayload, MediumRayleigh ) == 112 );
    static_assert( offsetof( SkyGpuPayload, MediumMie ) == 128 );
    static_assert( offsetof( SkyGpuPayload, MediumMieAbsorption ) == 144 );
    static_assert( offsetof( SkyGpuPayload, MediumOzone ) == 160 );
    static_assert( offsetof( SkyGpuPayload, MediumGround ) == 176 );
    static_assert( offsetof( SkyGpuPayload, MediumTentPlanet ) == 192 );

    // Size the buffer is created with, and the size every writer must hand to SetData.
    inline constexpr uint32_t kSkyPayloadBytes =
         kSkyPackedVec4Count * static_cast<uint32_t>( sizeof( glm::vec4 ) );

    // Binding number the sky parameter buffer is declared at, in BOTH sky shaders.
    //
    // It has to be one number, not two. The graphics descriptor write uses the buffer's OWN binding
    // (VulkanMaterialBackend::ApplyStorageBuffer -> VulkanStorageBuffer::GetBinding), while
    // ComputePipeline::SetStorageBuffer takes the binding as an explicit ARGUMENT. When those two disagree
    // the buffer lands on somebody else's slot — the particle system aliased the camera uniform buffer at
    // binding 0 exactly this way and tripped VUID-VkWriteDescriptorSet-descriptorType-00319. So: the buffer
    // is created with this number, both shaders declare this number, and the compute dispatch passes it.
    inline constexpr uint32_t kSkyPayloadBinding = 1;

    // The bindings the atmosphere-LUT compute passes agree on with their shaders — same explicit-argument
    // trap as kSkyPayloadBinding, same cure: one constant, both sides.
    inline constexpr uint32_t kSkyTransmittanceLutOutputBinding = 0; // SkyTransmittanceLut: the image it fills
    inline constexpr uint32_t kSkyMultiScatterLutOutputBinding  = 0; // SkyMultiScatterLut: the image it fills
    inline constexpr uint32_t kSkyTransmittanceLutBinding       = 2; // SkyMultiScatterLut: transmittance input

    // @p towardSun must be normalized — the single normalization lives in ECS::Rules::AtmosphereSunDirection.
    inline SkyGpuPayload PackSky( const glm::vec3& towardSun, const SkySettings& sky )
    {
        SkyGpuPayload payload;
        payload.SunDirection = glm::vec4( towardSun, sky.SunIntensity );
        payload.Zenith       = glm::vec4( sky.ZenithColor, sky.SkyBrightness );
        payload.Horizon      = glm::vec4( sky.HorizonColor, sky.HorizonFalloff );
        payload.Sun          = glm::vec4( sky.SunColor, sky.SunGlow );
        payload.Sunset       = glm::vec4( sky.SunsetColor, sky.SunsetIntensity );
        payload.Ground       = glm::vec4( sky.GroundColor, sky.StarIntensity );
        payload.Night        = glm::vec4( sky.NightColor, sky.SunAngularRadius );

        payload.MediumRayleigh      = glm::vec4( sky.RayleighScattering, sky.RayleighExpDistributionKm );
        payload.MediumMie           = glm::vec4( sky.MieScattering, sky.MieExpDistributionKm );
        payload.MediumMieAbsorption = glm::vec4( sky.MieAbsorption, sky.MieAnisotropy );
        payload.MediumOzone         = glm::vec4( sky.OzoneAbsorption, sky.AtmosphereHeightKm );
        payload.MediumGround        = glm::vec4( sky.GroundAlbedo, sky.MultiScatteringFactor );
        payload.MediumTentPlanet =
             glm::vec4( sky.OzoneTipAltitudeKm, sky.OzoneTipValue, sky.OzoneTentWidthKm, sky.PlanetRadius );
        return payload;
    }
} // namespace Desert::Graphic
