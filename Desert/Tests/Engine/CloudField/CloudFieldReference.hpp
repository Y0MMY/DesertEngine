#pragma once

// Compiles Editor/Resources/Shaders/Common/CloudField.glslh AS C++, together with the two headers it
// requires: Common/CloudNoise.glslh (the noise it is fed) and Common/CloudGeometry.glslh (the types and
// the units it shares).
//
// THE NOISE CALLBACK IS THE POINT OF THE SEAM. CloudField.glslh declares no sampler; it asks for the four
// octaves through CLOUD_SAMPLE_NOISE, which the march defines as a fetch of the baked volume and this
// header defines as a call into the very functions that BAKED it — same noises, same periods, same seed
// offsets as Programs/Clouds/CloudNoiseBake.shader. A test written against a different field would be a
// test of a different sky.
//
// The two differences from the GPU path are deliberate and stated rather than discovered:
//   * the volume is RGBA8, so the GPU sees the field quantized to 1/255 and trilinearly interpolated
//     between 128 voxels per axis, while this evaluates it analytically. Nothing asserted here is finer
//     than that quantization.
//   * the seeds and octave counts are the component's defaults, because that is what the shipped bake
//     writes; a different seed moves individual clouds and not one of the statistics measured here.
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

        // Programs/Clouds/CloudNoiseBake.shader, channel for channel. The seeds are the component's
        // Weather Seed and Detail Seed defaults and the octave counts its Weather Octaves and Detail
        // Octaves defaults; the per-channel offsets, periods and curl strength are the bake's own.
        constexpr uint  kCoverageSeed    = 1337u;
        constexpr uint  kErosionSeed     = 13u;
        constexpr int   kCoverageOctaves = 3;
        constexpr int   kErosionOctaves  = 2;
        constexpr float kCurlStrength    = 0.33f;

        vec4 CloudEvaluateBakedVolume( vec3 texturePosition )
        {
            const float r =
                 CloudPerlinWorley01( texturePosition * 4.0f, 4.0f, kCoverageSeed + 0u, kCoverageOctaves );
            const float g =
                 CloudPerlinWorley01( texturePosition * 8.0f, 8.0f, kCoverageSeed + 977u, kCoverageOctaves );
            const float b =
                 CloudWorleyFbm( texturePosition * 12.0f, 12.0f, kErosionSeed + 1861u, kErosionOctaves );
            const float a = CloudCurlyPerlin01( texturePosition * 16.0f, 16.0f, kErosionSeed + 2749u,
                                                kErosionOctaves, kCurlStrength );
            return vec4( r, g, b, a );
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
