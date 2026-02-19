#include "SkinnedMaterialPBR.hpp"

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic
{
    void SkinnedMaterialPBR::Bind( const UpdateSkinnedMaterialPBRInfo& info )
    {
        UpdateCamera( *this, info.MainCamera );
        UpdatePointLights( *this, info.PointLights );
        UpdateDirectionLights( *this, info.DirectionLights );
        UpdateLightsMetadata( *this, info.PointLights, info.DirectionLights );

        UpdatePBRTextures( *this, info.PBREnvTextures );
        UpdatePBRMaterial( *this, {} );

        UpdateSkinnedUB( info.SkinnedUB );

        UpdateTextures( m_MaterialExecutor.get() );

        m_MaterialExecutor->PushConstant( &info.MeshTransform, sizeof( glm::mat4 ) );
    }

    void SkinnedMaterialPBR::UpdateSkinnedUB( const ShaderProtocols::SkinnedUB& skinnedUB )
    {
        static ShaderProtocols::SkinnedUB SkinnedUB;

        SkinnedUB.BoneMatrices = skinnedUB.BoneMatrices;

        Get<StorageBufferProperty>( SkinnedUB.Name )
             ->SetRawData( SkinnedUB.BoneMatrices.data(), sizeof( glm::mat4 ) * SkinnedUB.BoneMatrices.size() );
    }

} // namespace Desert::Graphic
