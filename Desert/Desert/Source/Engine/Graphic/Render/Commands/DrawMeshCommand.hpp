#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Geometry/Mesh.hpp>
#include <glm/mat4x4.hpp>

namespace Desert::Graphic::Render
{
    struct DrawStaticMeshCommand : RenderCommand
    {
        std::shared_ptr<Desert::Mesh>               Mesh;
        std::shared_ptr<Graphic::StaticMaterialPBR> Material;
        glm::mat4                                   Transform;

        DrawStaticMeshCommand( std::shared_ptr<Desert::Mesh>               mesh,
                               std::shared_ptr<Graphic::StaticMaterialPBR> material, const glm::mat4& transform )
             : Mesh( std::move( mesh ) ), Material( std::move( material ) ), Transform( transform )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.AddStaticMesh( Mesh, Material, Transform );
        }
    };
} // namespace Desert::Graphic::Render