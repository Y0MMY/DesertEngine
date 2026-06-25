#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Assets/MaterialAsset.hpp>

#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>
#include <Engine/Graphic/ShaderProtocols/DirectionLight.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    class Image2D;
    class ImageCube;

    // Shared base for PBR materials: provides the per-frame scene-data uploads (camera + lights) into
    // the shared executor uniform buffers. Material parameters themselves live in the reflected
    // Assets::PBRMaterialData and travel via push constants (see PBRPush.hpp), not through this base.
    class MaterialPBRBase : public Material
    {
    public:
        static void UpdateCamera( MaterialInstance* instance, const Core::Camera* camera );
        static void UpdateLights( MaterialInstance* instance, const ShaderProtocols::PointLight& pointLights,
                                  const ShaderProtocols::DirectionLight& dirLights );
        // Binds the directional shadow map + its light view-projection (and bias/enable) for the PBR pass.
        static void UpdateShadow( MaterialInstance* instance, const glm::mat4& lightViewProj, Image2D* shadowMap,
                                  float bias, bool enabled, bool debugVisualize = false );
        // Binds the IBL inputs for the PBR pass: diffuse irradiance + prefiltered specular cubemaps and
        // the (precomputed) split-sum BRDF LUT.
        static void UpdateEnvironment( MaterialInstance* instance, ImageCube* irradiance, ImageCube* prefiltered,
                                       Image2D* brdfLut );

    protected:
        MaterialPBRBase( std::string&& debugName, std::string&& shaderName );
        ~MaterialPBRBase() override = default;

        static void UpdatePointLights( MaterialInstance* instance, const ShaderProtocols::PointLight& lights );
        static void UpdateDirectionLights( MaterialInstance* instance, const ShaderProtocols::DirectionLight& lights );
        static void UpdateLightsMetadata( MaterialInstance* instance, const ShaderProtocols::PointLight& point,
                                          const ShaderProtocols::DirectionLight& dir );
    };
} // namespace Desert::Graphic
