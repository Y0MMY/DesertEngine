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
        bool                                    Outlined    = false;
        bool                                    CastShadows = true;

        DrawSkinnedMeshCommand( Desert::SkinnedMesh*                           mesh,
                                const std::vector<Graphic::MaterialInstance*>& materialSlot,
                                const glm::mat4& transform, const std::vector<glm::mat4>& bones,
                                bool outlined = false, bool castShadows = true )
             : Mesh( mesh ), MaterialSlot( materialSlot ), Transform( transform ), BoneMatrices( bones ),
               Outlined( outlined ), CastShadows( castShadows )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            // CastShadows must survive this hop: the flag is consumed three links away (the cascade pass
            // skips !CastShadows on SkinnedMeshRenderData), and a default here would silently re-enable
            // the shadow for every skinned mesh whose component turned it off.
            renderer.SubmitMesh(
                 Mesh, MaterialSlot, Transform,
                 { .BoneMatrices = BoneMatrices, .Outlined = Outlined, .CastShadows = CastShadows } );
        }
    };
} // namespace Desert::Graphic::Render