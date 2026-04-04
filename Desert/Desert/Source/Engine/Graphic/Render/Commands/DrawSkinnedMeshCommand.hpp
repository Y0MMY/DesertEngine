#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Animation/Pose.hpp>

namespace Desert::Graphic::Render
{
    struct DrawSkinnedMeshCommand : RenderCommand
    {
        std::shared_ptr<Desert::SkinnedMesh>         Mesh;
        std::shared_ptr<Graphic::SkinnedMaterialPBR> Material;
        glm::mat4                                    Transform;
        std::vector<glm::mat4>                       BoneMatrices;

        DrawSkinnedMeshCommand( std::shared_ptr<Desert::SkinnedMesh>         mesh,
                                std::shared_ptr<Graphic::SkinnedMaterialPBR> material, const glm::mat4& transform,
                                const std::vector<glm::mat4>& bones )
             : Mesh( std::move( mesh ) ), Material( std::move( material ) ), Transform( transform ),
               BoneMatrices( bones )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.AddSkinnedMesh( Mesh, Material, Transform, BoneMatrices );
        }
    };
} // namespace Desert::Graphic::Render