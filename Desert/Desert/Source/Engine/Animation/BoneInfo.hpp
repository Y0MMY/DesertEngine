#pragma once

#include <glm/glm.hpp>
#include <string>
#include <optional>

namespace Desert::Animation
{
    struct BoneInfo
    {
        std::string Name;

        // Inverse bind pose (mesh space > bone space)
        glm::mat4 OffsetMatrix;

        // Local transform â bind pose (Assimp)
        glm::mat4 LocalBindTransform;

        std::optional<uint32_t> ParentBoneID;

        [[nodiscard]] bool IsRoot() const
        {
            return !ParentBoneID.has_value();
        }

        [[nodiscard]] uint32_t GetParentID() const
        {
            return ParentBoneID.value_or( UINT32_MAX );
        }
    };
} // namespace Desert::Animation
