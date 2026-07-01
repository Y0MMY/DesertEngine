#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Desert::Graphic
{
    // Per-draw overrides for a data-driven material: named shader params (vec4) + named texture-slot bindings
    // (texture asset handle). These two ALWAYS travel together through the whole submission chain (command ->
    // SceneRenderer -> renderer), so they're one type instead of two parallel vectors duplicated everywhere.
    struct MaterialOverrides
    {
        std::vector<std::pair<std::string, glm::vec4>> Params;   // shader param name -> value
        std::vector<std::pair<std::string, uint64_t>>  Textures; // sampler name -> texture asset handle
    };
} // namespace Desert::Graphic
