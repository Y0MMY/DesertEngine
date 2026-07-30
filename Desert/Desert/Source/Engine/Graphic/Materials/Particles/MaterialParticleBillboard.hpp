#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/Properties/StorageBufferProperty.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>
#include <Engine/Graphic/ShaderProtocols/Camera.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <memory>

namespace Desert::Graphic
{
    // Billboard material for the GPU particle system. Feeds only the shared CameraUB + the particle storage
    // buffer (the "ParticleBillboard" shader reads size/colour that the compute pass baked into each particle,
    // so there are no per-emitter uniforms). The particle buffer is EXTERNALLY owned (compute-written,
    // persistent) and rebound each draw via StorageBufferProperty::SetBuffer.
    class MaterialParticleBillboard final : public Material
    {
    public:
        MaterialParticleBillboard() : Material( "MaterialParticleBillboard", "ParticleBillboard" )
        {
        }

        void Update( const Core::Camera* camera, const std::shared_ptr<ShaderResources::StorageBuffer>& particles )
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

            if ( auto* sb = Get<StorageBufferProperty>( "Particles" ) )
                sb->SetBuffer( particles );
        }
    };
} // namespace Desert::Graphic
