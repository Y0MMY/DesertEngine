#pragma once

#include "MaterialPBRBase.hpp"

namespace Desert::Graphic
{
    class StaticMaterialPBR final : public Material, public MaterialPBRBase
    {
    public:
        struct UpdateMaterialPBRInfo
        {
            Core::Camera* MainCamera;
            glm::mat4     MeshTransform{ 1.0f };

            ShaderProtocols::DirectionLight DirectionLights;
            ShaderProtocols::PointLight     PointLights;
            ShaderProtocols::PBRTexturesUB               PBREnvTextures;
        };

        StaticMaterialPBR( const std::shared_ptr<Assets::MaterialAsset>& asset )
             : Material( "StaticMaterialPBR", "StaticMeshPBR" ), MaterialPBRBase{ asset }
        {
        }

        void Bind( const UpdateMaterialPBRInfo& info );
    };
} // namespace Desert::Graphic
