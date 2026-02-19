#pragma once

#include <Engine/Runtime/ImageHandle.hpp>

namespace Desert::Graphic::ShaderProtocols
{
    struct SkinnedUB
    {
        inline const static std::string Name = "Bones";

        std::vector<glm::mat4> BoneMatrices;
    };
} // namespace Desert::Graphic::ShaderProtocols
