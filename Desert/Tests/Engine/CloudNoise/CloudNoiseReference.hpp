#pragma once

// Compiles Editor/Resources/Shaders/Common/CloudNoise.glslh AS C++.
//
// This is not a port and not a paraphrase — it is the same file, the same text, that the three
// CloudNoise* compute shaders compile as GLSL. Vulkan does not run in this environment, so the only
// thing a hand-written CPU copy could prove is that the copy agrees with itself; the first tuning pass
// on either side would silently separate them. Sharing the text means a passing test is a statement
// about the code the GPU runs.
//
// How the two languages are reconciled:
//   * glm supplies vec2/vec3/vec4 and floor/fract/mix/clamp/dot/min/sqrt/abs with GLSL semantics;
//   * uvec3/ivec3/uint are typedef'd to their glm / cstdint equivalents;
//   * the include sits inside an ANONYMOUS namespace so each translation unit gets its own copy of the
//     functions — GLSL has no `inline`, so the shared text cannot carry one, and without this a second
//     test TU including this header would be an ODR violation at link time.
// The glslh is written in the GLSL/C++ intersection; anything outside it breaks one of the two builds
// straight away, which is the property that keeps the sharing honest.

#include <glm/glm.hpp>

#include <cstdint>

namespace Desert::Tests::CloudNoiseRef
{
    // The shared file is compiled with these names in scope. They are declared inside the namespace, not
    // globally, so nothing else in a test binary sees a bare `vec3`.
    namespace
    {
        using vec2  = glm::vec2;
        using vec3  = glm::vec3;
        using vec4  = glm::vec4;
        using ivec3 = glm::ivec3;
        using uvec3 = glm::uvec3;
        using uint  = std::uint32_t;

        using glm::abs;
        using glm::clamp;
        using glm::dot;
        using glm::floor;
        using glm::max;
        using glm::min;
        using glm::sqrt;

// The shader root is on the include path (see this test's premake5.lua) so the path below is spelled
// exactly the way the shader spells it — one string to keep in step instead of two.
#include <Common/CloudNoise.glslh>

    } // namespace
} // namespace Desert::Tests::CloudNoiseRef
