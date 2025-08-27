#pragma once

#include <Engine/Graphic/Materials/Mesh/PBR/MaterialPBR.hpp>

namespace Desert::Graphic
{
    struct MeshRenderData
    {
        std::shared_ptr<Mesh>        Mesh;
        glm::mat4                    Transform;
        std::shared_ptr<MaterialPBR> Material;

        bool Outlined = false;
    };
} // namespace Desert::Graphic