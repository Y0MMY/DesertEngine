#include "SkinnedMaterialPBR.hpp"

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic
{
    void SkinnedMaterialPBR::Bind( const UpdateSkinnedMaterialPBRInfo& info )
    {
        UpdateCamera( info.instance, info.MainCamera );
        UpdatePointLights( info.instance, info.PointLights );
        UpdateDirectionLights( info.instance, info.DirectionLights );
        UpdateLightsMetadata( info.instance, info.PointLights, info.DirectionLights );

        UpdatePBRTextures( info.instance, info.PBREnvTextures );
        UpdatePBRMaterial( info.instance, {} );

        UpdateSkinnedUB( info.instance, info.SkinnedUB );

        // Apply all properties and bind the material
        MaterialPBRBase::Bind( info.instance );
    }

    void SkinnedMaterialPBR::UpdateSkinnedUB( MaterialInstance* instance, const ShaderProtocols::SkinnedUB& skinnedUB )
    {
        static ShaderProtocols::SkinnedUB SkinnedUB;

        SkinnedUB.BoneMatrices = skinnedUB.BoneMatrices;

        Get<StorageBufferProperty>( SkinnedUB.Name )
             ->SetRawData( SkinnedUB.BoneMatrices.data(), sizeof( glm::mat4 ) * SkinnedUB.BoneMatrices.size() );
    }

} // namespace Desert::Graphic
