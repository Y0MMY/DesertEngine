#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Assets/MaterialAsset.hpp>

#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>
#include <Engine/Graphic/ShaderProtocols/DirectionLight.hpp>
#include <Engine/Graphic/ShaderProtocols/PBRTextures.hpp>
#include <Engine/Graphic/ShaderProtocols/PBRMeshMaterials.hpp>

namespace Desert::Graphic
{
    class MaterialPBRBase : public Material
    {
    public:
        struct PBRMaterialData
        {
            Image2D* Albedo    = nullptr;
            Image2D* Normal    = nullptr;
            Image2D* Metallic  = nullptr;
            Image2D* Roughness = nullptr;
            Image2D* AO        = nullptr;
            Image2D* Emissive  = nullptr;

            float MetallicFactor  = 0.0f;
            float RoughnessFactor = 1.0f;
        };

        static void UpdateCamera( MaterialInstance* instance, const Core::Camera* camera );
        static void UpdateLights( MaterialInstance* instance, const ShaderProtocols::PointLight& pointLights,
                                  const ShaderProtocols::DirectionLight& dirLights );

    protected:
        MaterialPBRBase( std::string&& debugName, std::string&& shaderName, const PBRMaterialData& data );
        ~MaterialPBRBase() override = default;

        static void UpdatePointLights( MaterialInstance* instance, const ShaderProtocols::PointLight& lights );
        static void UpdateDirectionLights( MaterialInstance* instance, const ShaderProtocols::DirectionLight& lights );
        static void UpdateLightsMetadata( MaterialInstance* instance, const ShaderProtocols::PointLight& point,
                                          const ShaderProtocols::DirectionLight& dir );

        static void UpdatePBRTextures( MaterialInstance* instance, const ShaderProtocols::PBRTexturesUB& textures );
        static void UpdatePBRMaterial( MaterialInstance* instance, const ShaderProtocols::PBRMeshMaterialsUB& meshUB );

    protected:
        PBRMaterialData m_RuntimeData;
    };
} // namespace Desert::Graphic
