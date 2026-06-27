#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/Properties/StorageBufferProperty.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/Camera.hpp>

#include <glm/glm.hpp>

#include <vector>

namespace Desert::Graphic
{
    // World-space debug line list (AABB wireframes, gizmos, ...). Endpoints live in a storage buffer the
    // "DebugLine" shader pulls by gl_VertexIndex; drawn via Renderer::SubmitLines on a Lines-topology
    // pipeline. Feeds only the shared CameraUB + the Lines storage buffer.
    class MaterialDebugLine final : public Material
    {
    public:
        struct LineVertex
        {
            glm::vec4 PositionWS; // xyz world position
            glm::vec4 Color;      // rgba
        };

        MaterialDebugLine() : Material( "MaterialDebugLine", "DebugLine" )
        {
        }

        void Update( const Core::Camera* camera, const std::vector<LineVertex>& lines )
        {
            if ( camera )
            {
                ShaderProtocols::Camera cam;
                cam.View       = camera->GetViewMatrix();
                cam.Projection = camera->GetProjectionMatrix();
                cam.CameraPos  = camera->GetPosition();
                if ( auto* ub = Get<UniformBufferProperty>( ShaderProtocols::Camera::Name ) )
                    ub->SetRawData( reinterpret_cast<const std::byte*>( &cam ), sizeof( cam ) );
            }

            if ( !lines.empty() )
                if ( auto* sb = Get<StorageBufferProperty>( "Lines" ) )
                    sb->SetRawData( const_cast<LineVertex*>( lines.data() ),
                                    static_cast<uint32_t>( lines.size() * sizeof( LineVertex ) ) );
        }
    };
} // namespace Desert::Graphic
