#pragma once

// Compiles Editor/Resources/Shaders/Common/CloudGeometry.glslh AS C++.
//
// This is not a port and not a paraphrase — it is the same file, the same text, that
// Programs/Clouds/CloudRaymarch.shader compiles as GLSL. Vulkan does not run in this environment, so the
// marched picture cannot be looked at; the only thing a hand-written CPU copy could prove is that the
// copy agrees with itself, and the first tuning pass on either side would separate them silently.
// Sharing the text means a passing test is a statement about the code the GPU runs.
//
// The arrangement mirrors Tests/Engine/CloudNoise/CloudNoiseReference.hpp, which does the same for the
// noise header:
//   * glm supplies vec2/vec3/vec4/mat4 and the maths built-ins with GLSL semantics;
//   * the include sits inside an ANONYMOUS namespace so each translation unit gets its own copy — GLSL
//     has no `inline`, so the shared text cannot carry one;
//   * the structs the header returns are C++ aggregates, and C++20 accepts the parenthesised
//     constructor syntax GLSL requires for them (P0960).
// The glslh is written in the GLSL/C++ intersection; anything outside it breaks one of the two builds
// straight away, which is the property that keeps the sharing honest.

#include <glm/glm.hpp>

#include <cstdint>

namespace Desert::Tests::CloudGeometryRef
{
    // The shared file is compiled with these names in scope. They are declared inside the namespace, not
    // globally, so nothing else in a test binary sees a bare `vec3`.
    namespace
    {
        using vec2 = glm::vec2;
        using vec3 = glm::vec3;
        using vec4 = glm::vec4;
        using mat4 = glm::mat4;

        using glm::abs;
        using glm::clamp;
        using glm::cos;
        using glm::cross;
        using glm::dot;
        using glm::exp;
        using glm::length;
        using glm::max;
        using glm::min;
        using glm::mix;
        using glm::normalize;
        using glm::pow;
        using glm::sin;
        using glm::sqrt;

#include <Common/CloudGeometry.glslh>

// The shadow map's projection and read-out, compiled from the same text the two shaders include. This is
// the header whose agreement matters most: the pass that FILLS the map and the march that READS it both
// project through it, and a disagreement puts every shadow somewhere other than the cloud that cast it.
#include <Common/CloudShadow.glslh>

    } // namespace
} // namespace Desert::Tests::CloudGeometryRef
