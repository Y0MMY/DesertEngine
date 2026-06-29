#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Animation/Pose.hpp>

namespace Desert::Graphic::Render
{
    struct DrawSkinnedMeshCommand : RenderCommand
    {
        Desert::SkinnedMesh*                    Mesh;
        std::vector<Graphic::MaterialInstance*> MaterialSlot;
        glm::mat4                               Transform;
        std::vector<glm::mat4>                  BoneMatrices;
        bool                                    Outlined = false;

        DrawSkinnedMeshCommand( Desert::SkinnedMesh* mesh, const std::vector<Graphic::MaterialInstance*>& materialSlot,
                                const glm::mat4& transform, const std::vector<glm::mat4>& bones,
                                bool outlined = false )
             : Mesh( mesh ), MaterialSlot( materialSlot ), Transform( transform ), BoneMatrices( bones ),
               Outlined( outlined )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SubmitMesh( Mesh, MaterialSlot, Transform,
                                 { .BoneMatrices = BoneMatrices, .Outlined = Outlined } );
        }
    };
} // namespace Desert::Graphic::Render