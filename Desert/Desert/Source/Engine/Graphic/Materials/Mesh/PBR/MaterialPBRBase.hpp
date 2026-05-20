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
    class MaterialPBRBase
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

    protected:
        explicit MaterialPBRBase( const PBRMaterialData& data );
        ~MaterialPBRBase() = default;

        // ---- Update helpers ----
        void UpdateCamera( Material& material, const Core::Camera* camera );
        void UpdatePointLights( Material& material, const ShaderProtocols::PointLight& lights );
        void UpdateDirectionLights( Material& material, const ShaderProtocols::DirectionLight& lights );
        void UpdateLightsMetadata( Material& material, const ShaderProtocols::PointLight& point,
                                   const ShaderProtocols::DirectionLight& dir );

        void UpdatePBRTextures( Material& material, const ShaderProtocols::PBRTexturesUB& textures );

        void UpdatePBRMaterial( Material& material, const ShaderProtocols::PBRMeshMaterialsUB& meshUB );

        void UpdateTextures( const MaterialExecutor* executor );

    protected:
        PBRMaterialData m_RuntimeData;
    };
} // namespace Desert::Graphic
