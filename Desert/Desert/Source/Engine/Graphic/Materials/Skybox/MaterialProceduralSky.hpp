#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/Camera.hpp>
#include <Engine/Graphic/CloudSettings.hpp>

#include <glm/glm.hpp>

#include <chrono>

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

        void Update( const Core::Camera* camera, const glm::vec3& sunDir, float sunIntensity, float sunDiskRadius,
                     const CloudSettings& clouds )
        {
            if ( !camera )
                return;

            ShaderProtocols::Camera cameraUB;
            cameraUB.View       = camera->GetViewMatrix();
            cameraUB.Projection = camera->GetProjectionMatrix();
            cameraUB.CameraPos  = camera->GetPosition();
            if ( auto* ub = Get<UniformBufferProperty>( ShaderProtocols::Camera::Name ) )
                ub->SetRawData( reinterpret_cast<const std::byte*>( &cameraUB ), sizeof( cameraUB ) );

            // Real-time elapsed seconds for cloud animation (self-contained; no per-frame dt needed).
            static const auto start  = std::chrono::steady_clock::now();
            const float       time   = std::chrono::duration<float>(
                                          std::chrono::steady_clock::now() - start ).count();

            struct SkyUBData
            {
                glm::vec4 SunDirection; // xyz toward sun, w intensity
                glm::vec4 SkyParams;    // x sun angular radius, y clouds enabled, z coverage, w density
                glm::vec4 CloudParams;  // x height(base), y thickness, z time, w wind speed
                glm::vec4 CameraPos;    // xyz world camera position
            } sky;
            sky.SunDirection = glm::vec4( glm::normalize( sunDir ), sunIntensity );
            sky.SkyParams    = glm::vec4( sunDiskRadius, clouds.Enabled ? 1.0f : 0.0f, clouds.Coverage,
                                          clouds.Density );
            sky.CloudParams  = glm::vec4( clouds.Height, clouds.Thickness, time, clouds.WindSpeed );
            sky.CameraPos    = glm::vec4( camera->GetPosition(), 0.0f );
            if ( auto* ub = Get<UniformBufferProperty>( "SkyUB" ) )
                ub->SetRawData( reinterpret_cast<const std::byte*>( &sky ), sizeof( sky ) );
        }
    };
} // namespace Desert::Graphic
