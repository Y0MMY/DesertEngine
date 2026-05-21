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

        DrawStaticMeshCommand( Desert::Mesh* mesh, const std::vector<Graphic::MaterialInstance*>& materialSlots,
                               const glm::mat4& transform )
             : Mesh( mesh ), MaterialSlots( materialSlots ), Transform( transform )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SubmitMesh( Mesh, MaterialSlots, Transform, {} );
        }
    };
} // namespace Desert::Graphic::Render