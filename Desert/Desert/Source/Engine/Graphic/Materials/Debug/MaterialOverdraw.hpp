#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>
#include <Engine/Graphic/ShaderProtocols/Camera.hpp>
#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    // Overdraw accumulation material: only feeds the shared camera UB (the per-mesh transform is pushed
    // automatically by Renderer::RenderMesh). Drives Overdraw.shader. Header-only (no new .cpp -> no premake
    // regen).
    class MaterialOverdraw final : public Material
    {
    public:
        MaterialOverdraw() : Material( "MaterialOverdraw", "Overdraw" )
        {
        }

        void UpdateCamera( const Core::Camera* camera )
        {
            if ( !camera )
                return;

            ShaderProtocols::Camera cameraUB;
            cameraUB.Projection = camera->GetProjectionMatrix();
            cameraUB.View       = camera->GetViewMatrix();
            cameraUB.CameraPos  = camera->GetPosition();

            Get<UniformBufferProperty>( ShaderProtocols::Camera::Name )
                 ->SetRawData( reinterpret_cast<const std::byte*>( &cameraUB ), sizeof( cameraUB ) );
        }
    };
} // namespace Desert::Graphic
