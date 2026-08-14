#pragma once

// Compiles Editor/Resources/Shaders/Common/CloudTemporal.glslh AS C++, on top of CloudGeometry.glslh.
//
// Not a port and not a paraphrase — the same text, the same file, that CloudTemporalResolve.shader,
// CloudComposite.shader and CloudRaymarch.shader compile as GLSL. Vulkan does not run in this
// environment, so a resolved frame cannot be looked at; the only thing a hand-written CPU copy could
// prove is that the copy agrees with itself, and the first tuning pass on either side would separate them
// silently. Sharing the text means a passing test is a statement about the code the GPU runs.
//
// The arrangement mirrors Tests/Engine/CloudMath/CloudGeometryReference.hpp exactly:
//   * glm supplies vec2/vec3/vec4/mat4 and the maths built-ins with GLSL semantics;
//   * the include sits inside an ANONYMOUS namespace so each translation unit gets its own copy — GLSL
//     has no `inline`, so the shared text cannot carry one;
//   * CloudGeometry.glslh comes first because CloudTemporal.glslh is written against it (the sphere
//     intersection and the kilometre constant), which is the same order the shaders include them in.

#include <glm/glm.hpp>

#include <cstdint>

namespace Desert::Tests::CloudTemporalRef
{
    namespace
    {
        using vec2  = glm::vec2;
        using vec3  = glm::vec3;
        using vec4  = glm::vec4;
        using mat4  = glm::mat4;
        using ivec2 = glm::ivec2;
        using uint  = std::uint32_t;

        using glm::abs;
        using glm::clamp;
        using glm::cos;
        using glm::cross;
        using glm::dot;
        using glm::exp;
        using glm::floor;
        using glm::length;
        using glm::max;
        using glm::min;
        using glm::mix;
        using glm::normalize;
        using glm::pow;
        using glm::sin;
        using glm::sqrt;

#include <Common/CloudGeometry.glslh>
#include <Common/CloudTemporal.glslh>

    } // namespace
} // namespace Desert::Tests::CloudTemporalRef
