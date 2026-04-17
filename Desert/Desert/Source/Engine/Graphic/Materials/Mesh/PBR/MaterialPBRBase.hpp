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
    protected:
        explicit MaterialPBRBase( const std::shared_ptr<Assets::MaterialAsset>& asset );
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
        // weak_ptr because AssetManager owns MaterialAsset
        // MaterialPBR only observes the base material
        std::weak_ptr<Assets::MaterialAsset> m_BaseMaterial;
    };
} // namespace Desert::Graphic
