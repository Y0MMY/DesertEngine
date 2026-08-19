#pragma once

// Compiles Editor/Resources/Shaders/Common/CloudField.glslh AS C++, together with the two headers it
// requires: Common/CloudNoise.glslh (the noise it is fed) and Common/CloudGeometry.glslh (the types and
// the units it shares).
//
// THE NOISE CALLBACK IS THE POINT OF THE SEAM. CloudField.glslh declares no sampler; it asks for the four
// octaves through CLOUD_SAMPLE_NOISE, which the march defines as a fetch of the baked volume and this
// header defines as a call into the very function that GENERATES it — CloudNoiseVolumeChannels, the same
// text Engine/Assets/CloudNoiseVolumeGenerator.cpp compiles to write the file. A test written against a
// different field would be a test of a different sky.
//
// The two differences from the GPU path are deliberate and stated rather than discovered:
//   * the volume is RGBA8, so the GPU sees the field quantized to 1/255 and trilinearly interpolated
//     between 128 voxels per axis, while this evaluates it analytically. Nothing asserted here is finer
//     than that quantization.
//   * the parameters are the volume asset's defaults, because that is what the shipped
//     CloudNoise_Default.dcnv carries; a different seed moves individual clouds and not one of the
//     statistics measured here.
//
// THE RESULTS ARE MEMOIZED. The coverage measurement evaluates the same grid of positions once per
// Coverage setting, and the field depends only on the position — so the cache turns a six-fold cost into
// a one-fold one and changes no answer. It lives here rather than in the test because the macro has to be
// defined before CloudField.glslh is included.

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <map>

namespace Desert::Tests::CloudFieldRef
{
    namespace
    {
        using vec2 = glm::vec2;
        using vec3 = glm::vec3;
        using vec4 = glm::vec4;

        using uint = std::uint32_t;

        using glm::clamp;
        using glm::dot;
        using glm::floor;
        using glm::length;
        using glm::max;
        using glm::min;
        using glm::mix;
        using glm::mod;
        using glm::pow;
        using glm::smoothstep;
        using glm::sqrt;

#include <Common/CloudNoise.glslh>
#include <Common/CloudGeometry.glslh>

        // ONE call into the ONE function that writes the volume — Common/CloudNoise.glslh's
        // CloudNoiseVolumeChannels — rather than four calls this file has to keep in step with the
        // generator's. The four-line copy that used to be here mirrored the compute bake and was a second
        // statement of the channel layout; it is exactly the shape of duplication that agrees with itself
        // until the first tuning pass.
        //
        // The parameters are the defaults of Engine/Assets/CloudNoiseVolume.hpp's CloudNoiseVolumeParams,
        // which is what the shipped CloudNoise_Default.dcnv was baked with. A different seed moves
        // individual clouds and not one of the statistics measured here.
        constexpr uint  kVolumeSeed     = 1337u;
        constexpr float kCurlStrength   = 0.33f;
        constexpr float kWispyPeriodLF  = 2.0f;
        constexpr float kWispyPeriodHF  = 4.0f;
        constexpr float kBillowPeriodLF = 3.0f;
        constexpr float kBillowPeriodHF = 6.0f;

        vec4 CloudEvaluateBakedVolume( vec3 texturePosition )
        {
            return CloudNoiseVolumeChannels( texturePosition, kVolumeSeed, kCurlStrength, kWispyPeriodLF,
                                             kWispyPeriodHF, kBillowPeriodLF, kBillowPeriodHF );
        }

        // Exact-key memoization: the key is the bit pattern of the three coordinates, so two callers that
        // ask for the same position get the same answer and nothing is ever interpolated.
        using NoiseKey = std::array<std::uint32_t, 3>;

        std::map<NoiseKey, vec4>& NoiseCache()
        {
            static std::map<NoiseKey, vec4> cache;
            return cache;
        }

        vec4 CloudSampleBakedVolumeCached( vec3 texturePosition )
        {
            NoiseKey key{};
            std::memcpy( key.data(), &texturePosition.x, sizeof( float ) );
            std::memcpy( key.data() + 1, &texturePosition.y, sizeof( float ) );
            std::memcpy( key.data() + 2, &texturePosition.z, sizeof( float ) );

            auto&      cache = NoiseCache();
            const auto it    = cache.find( key );
            if ( it != cache.end() )
                return it->second;

            const vec4 value = CloudEvaluateBakedVolume( texturePosition );
            cache.emplace( key, value );
            return value;
        }

#define CLOUD_SAMPLE_NOISE( p ) CloudSampleBakedVolumeCached( p )

#include <Common/CloudField.glslh>

    } // namespace
} // namespace Desert::Tests::CloudFieldRef
