#pragma once

#include <Common/Core/UUID.hpp>
#include <Engine/Assets/Common.hpp>

#include <optional>
#include <vector>
#include <glm/glm.hpp>

namespace Desert::Assets
{
    struct EntityData
    {
        Common::UUID id;
        Common::UUID parent;

        std::optional<AssetHandle> PrefabRef;

        std::optional<std::string> Tag;
        std::optional<glm::vec3>   Translation;
        std::optional<glm::vec3>   Rotation;
        std::optional<glm::vec3>   Scale;
    };

    struct PrefabData
    {
        std::vector<EntityData> Entities;
        Common::UUID            Root;
    };
} // namespace Desert::Assets