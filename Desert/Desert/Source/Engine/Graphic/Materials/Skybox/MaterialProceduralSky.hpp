#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/Camera.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // Fully engine-generated sky (no HDR asset): the "ProceduralSky" shader raymarches a Rayleigh+Mie
    // atmosphere from the view ray and the sun direction. This material only feeds the shared CameraUB
    // (for the view-ray reconstruction) and a small SkyUB (sun direction/intensity + sun disk size).
    class MaterialProceduralSky final : public Material
    {
    public:
        MaterialProceduralSky() : Material( "MaterialProceduralSky", "ProceduralSky" )
        {
        }

        void Update( const Core::Camera* camera, const glm::vec3& sunDir, float sunIntensity, float sunDiskRadius )
        {
            if ( !camera )
                return;

            ShaderProtocols::Camera cameraUB;
            cameraUB.View       = camera->GetViewMatrix();
            cameraUB.Projection = camera->GetProjectionMatrix();
            cameraUB.CameraPos  = camera->GetPosition();
            if ( auto* ub = Get<UniformBufferProperty>( ShaderProtocols::Camera::Name ) )
                ub->SetRawData( reinterpret_cast<const std::byte*>( &cameraUB ), sizeof( cameraUB ) );

            struct SkyUBData
            {
                glm::vec4 SunDirection; // xyz toward sun, w intensity
                glm::vec4 SkyParams;    // x sun angular radius
            } sky;
            sky.SunDirection = glm::vec4( glm::normalize( sunDir ), sunIntensity );
            sky.SkyParams    = glm::vec4( sunDiskRadius, 0.0f, 0.0f, 0.0f );
            if ( auto* ub = Get<UniformBufferProperty>( "SkyUB" ) )
                ub->SetRawData( reinterpret_cast<const std::byte*>( &sky ), sizeof( sky ) );
        }
    };
} // namespace Desert::Graphic
