#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Animation/Pose.hpp>

namespace Desert::Graphic::Render
{
    struct DrawSkinnedMeshCommand : RenderCommand
    {
        Desert::SkinnedMesh*            Mesh;
        std::vector<Graphic::Material*> MaterialSlot;
        glm::mat4                       Transform;
        std::vector<glm::mat4>          BoneMatrices;

        DrawSkinnedMeshCommand( Desert::SkinnedMesh* mesh, const std::vector<Graphic::Material*>& materialSlot,
                                const glm::mat4& transform, const std::vector<glm::mat4>& bones )
             : Mesh( mesh ), MaterialSlot( materialSlot ), Transform( transform ), BoneMatrices( bones )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SubmitMesh( Mesh, MaterialSlot, Transform, { .BoneMatrices = BoneMatrices } );
        }
    };
} // namespace Desert::Graphic::Render