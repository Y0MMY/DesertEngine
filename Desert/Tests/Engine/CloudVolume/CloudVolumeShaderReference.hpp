#pragma once

// Compiles Editor/Resources/Shaders/Common/CloudVolumeAtlas.glslh AS C++.
//
// This is not a port and not a paraphrase — it is the same file, the same text, that
// Common/CloudDensityVoxel.glslh compiles as GLSL to place every hero-cloud tap. Phase 1a's handover
// named the clamp inside it as the one thing most worth testing: it is the only thing keeping one hero
// cloud's voxels out of another's tile, and getting it subtly wrong renders as a stripe of the wrong
// cloud rather than as an error. Sharing the text means a passing test is a statement about the code the
// GPU runs, and the assertions in cloud_volume_test.cpp then check it against the C++ original in
// Engine/Graphic/Clouds/CloudVolumeAtlasLayout.hpp — two independent implementations of one mapping,
// which is exactly the class of defect this project's taxonomy is made of.
//
// The arrangement mirrors Tests/Engine/CloudMath/CloudGeometryReference.hpp:
//   * glm supplies vec3 and the maths built-ins with GLSL semantics;
//   * the include sits inside an ANONYMOUS namespace so each translation unit gets its own copy — GLSL
//     has no `inline`, so the shared text cannot carry one.

#include <glm/glm.hpp>

namespace Desert::Tests::CloudVolumeShaderRef
{
    namespace
    {
        using vec3 = glm::vec3;

        using glm::clamp;
        using glm::floor;
        using glm::mod;

#include <Common/CloudVolumeAtlas.glslh>

    } // namespace
} // namespace Desert::Tests::CloudVolumeShaderRef
