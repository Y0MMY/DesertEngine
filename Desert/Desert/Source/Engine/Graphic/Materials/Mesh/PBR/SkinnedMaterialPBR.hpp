#pragma once

#include "MaterialPBRBase.hpp"

#include <Engine/Graphic/ShaderProtocols/SkinnedMaterialUB.hpp>

namespace Desert::Graphic
{
    class SkinnedMaterialPBR final : public Material, public MaterialPBRBase
    {
    public:
        struct UpdateSkinnedMaterialPBRInfo
        {
            Core::Camera* MainCamera;
            glm::mat4     MeshTransform{ 1.0f };

            ShaderProtocols::DirectionLight DirectionLights;
            ShaderProtocols::PointLight     PointLights;
            ShaderProtocols::PBRTexturesUB               PBREnvTextures;

            ShaderProtocols::SkinnedUB SkinnedUB;
        };

        SkinnedMaterialPBR( const std::shared_ptr<Assets::MaterialAsset>& asset )
             : Material( "SkinnedMaterialPBR", "SkinnedMeshPBR" ), MaterialPBRBase( asset )
        {
        }

        void Bind( const UpdateSkinnedMaterialPBRInfo& info );

    private:
        void UpdateSkinnedUB( const ShaderProtocols::SkinnedUB& skinnedUB );
    };
} // namespace Desert::Graphic
