#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Geometry/Mesh.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <string>
#include <utility>
#include <vector>

namespace Desert::Graphic::Render
{
    // A static mesh drawn with a generic data-driven material (a MaterialComponent assigning a non-PBR
    // shader). Per-object path — does NOT go through the batched PBR SSBO.
    struct DrawGenericMeshCommand : RenderCommand
    {
        Desert::Mesh*                                  Mesh;
        glm::mat4                                      Transform;
        std::string                                    ShaderName;
        std::vector<std::pair<std::string, glm::vec4>> ParamOverrides;
        std::vector<std::pair<std::string, uint64_t>>  TextureOverrides;
        bool                                           Outlined = false;

        DrawGenericMeshCommand( Desert::Mesh* mesh, const glm::mat4& transform, std::string shaderName,
                                std::vector<std::pair<std::string, glm::vec4>> paramOverrides,
                                std::vector<std::pair<std::string, uint64_t>>  textureOverrides,
                                bool                                           outlined )
             : Mesh( mesh ), Transform( transform ), ShaderName( std::move( shaderName ) ),
               ParamOverrides( std::move( paramOverrides ) ), TextureOverrides( std::move( textureOverrides ) ),
               Outlined( outlined )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SubmitGenericMesh( Mesh, Transform, ShaderName, ParamOverrides, TextureOverrides, Outlined );
        }
    };
} // namespace Desert::Graphic::Render
