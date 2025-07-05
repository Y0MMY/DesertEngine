#pragma once

#include <Engine/Graphic/Materials/MaterialPBR.hpp>

namespace Desert::Graphic::DTO
{
    struct MeshRenderData
    {
        std::shared_ptr<Mesh>        Mesh;
        glm::mat4                    Transform;
        std::shared_ptr<MaterialPBR> Material;
    };
} // namespace Desert::Graphic::DTO