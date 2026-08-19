#pragma once

// Compiles Editor/Resources/Shaders/Common/CloudNoise.glslh AS C++.
//
// Not a port and not a paraphrase — the same text, the same file, that the cloud passes compile as GLSL.
// A rendered frame says the noise LOOKS like cloud; only this says it TILES, that its hash avalanches,
// and what range it actually occupies — three properties a frame cannot show and on which the rest of the
// shape model depends. The one thing a hand-written CPU copy could prove is that the copy agrees with
// itself, and the first tuning pass on either side would separate them silently.
//
// The arrangement is the house one for a shader-maths reference:
//   * glm supplies vec3/vec4 and the maths built-ins with GLSL semantics;
//   * `uint` is GLSL's spelling of a 32-bit unsigned word and has to be introduced by hand;
//   * the include sits inside an ANONYMOUS namespace so each translation unit gets its own copy — GLSL
//     has no `inline`, so the shared text cannot carry one.

#include <glm/glm.hpp>

#include <cstdint>

namespace Desert::Tests::CloudNoiseRef
{
    namespace
    {
        using vec3 = glm::vec3;
        using vec4 = glm::vec4;

        using uint = std::uint32_t;

        using glm::clamp;
        using glm::floor;
        using glm::max;
        using glm::min;
        using glm::mix;
        using glm::mod;
        using glm::pow;

#include <Common/CloudNoise.glslh>

    } // namespace
} // namespace Desert::Tests::CloudNoiseRef
