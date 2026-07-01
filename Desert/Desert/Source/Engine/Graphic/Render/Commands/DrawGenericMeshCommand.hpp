#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Graphic/Materials/MaterialOverrides.hpp>

#include <glm/mat4x4.hpp>

#include <string>
#include <utility>

namespace Desert::Graphic::Render
{
    // A static mesh drawn with a generic data-driven material (a MaterialComponent assigning a non-PBR
    // shader). Per-object path — does NOT go through the batched PBR SSBO.
    struct DrawGenericMeshCommand : RenderCommand
    {
        Desert::Mesh*            Mesh;
        glm::mat4                Transform;
        std::string              ShaderName;
        Graphic::MaterialOverrides Overrides;
        bool                     Outlined = false;

        DrawGenericMeshCommand( Desert::Mesh* mesh, const glm::mat4& transform, std::string shaderName,
                                Graphic::MaterialOverrides overrides, bool outlined )
             : Mesh( mesh ), Transform( transform ), ShaderName( std::move( shaderName ) ),
               Overrides( std::move( overrides ) ), Outlined( outlined )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SubmitGenericMesh( Mesh, Transform, ShaderName, Overrides, Outlined );
        }
    };
} // namespace Desert::Graphic::Render
