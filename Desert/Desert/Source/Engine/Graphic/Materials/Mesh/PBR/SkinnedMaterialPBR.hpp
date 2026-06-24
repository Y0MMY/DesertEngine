#pragma once

#include "MaterialPBRBase.hpp"
#include "PBRPush.hpp"

#include <Engine/Graphic/ShaderProtocols/SkinnedMaterialUB.hpp>

namespace Desert::Graphic
{
    // Skinned PBR material. Like StaticMaterialPBR, its parameters live in the reflected
    // Assets::PBRMaterialData and travel via push constants; additionally it uploads the bone matrices
    // (real skinning data) through a storage buffer.
    class SkinnedMaterialPBR final : public MaterialPBRBase
    {
    public:
        struct UpdateSkinnedMaterialPBRInfo
        {
            MaterialInstance* instance = nullptr;
            Core::Camera*     MainCamera = nullptr;
            glm::mat4         MeshTransform{ 1.0f };

            ShaderProtocols::DirectionLight DirectionLights;
            ShaderProtocols::PointLight     PointLights;
            ShaderProtocols::SkinnedUB      SkinnedUB;
        };

        SkinnedMaterialPBR() : MaterialPBRBase( "SkinnedMaterialPBR", "SkinnedMeshPBR" )
        {
        }

        Assets::PBRMaterialData&       Data()       { return m_Data; }
        const Assets::PBRMaterialData& Data() const { return m_Data; }

        void Bind( const UpdateSkinnedMaterialPBRInfo& info );

    private:
        void UpdateSkinnedUB( MaterialInstance* instance, const ShaderProtocols::SkinnedUB& skinnedUB );

        Assets::PBRMaterialData m_Data;
    };
} // namespace Desert::Graphic
