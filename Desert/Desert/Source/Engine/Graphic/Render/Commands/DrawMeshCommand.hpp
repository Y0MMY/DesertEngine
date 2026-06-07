#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Geometry/Mesh.hpp>
#include <glm/mat4x4.hpp>

namespace Desert::Graphic::Render
{
    struct DrawStaticMeshCommand : RenderCommand
    {
        Desert::Mesh*                           Mesh;
        std::vector<Graphic::MaterialInstance*> MaterialSlots;
        glm::mat4                       Transform;
        bool                            Outlined = false;

        DrawStaticMeshCommand( Desert::Mesh* mesh, const std::vector<Graphic::MaterialInstance*>& materialSlots,
                               const glm::mat4& transform, bool outlined = false )
             : Mesh( mesh ), MaterialSlots( materialSlots ), Transform( transform ), Outlined( outlined )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SubmitMesh( Mesh, MaterialSlots, Transform, { .Outlined = Outlined } );
        }
    };
} // namespace Desert::Graphic::Render