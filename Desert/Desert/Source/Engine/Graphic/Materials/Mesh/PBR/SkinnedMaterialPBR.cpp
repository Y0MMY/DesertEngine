#include "SkinnedMaterialPBR.hpp"

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic
{
    void SkinnedMaterialPBR::Bind( const UpdateSkinnedMaterialPBRInfo& info )
    {
        UpdateCamera( info.instance, info.MainCamera );
        UpdateLights( info.instance, info.PointLights, info.SpotLights, info.DirectionLights );
        UpdateSkinnedUB( info.instance, info.SkinnedUB );

        // Single-object material buffer (the skinned path draws one mesh at a time).
        const PBRGpuMaterial gpuMaterial = BuildPBRGpuMaterial( m_Data );
        if ( auto* sb = Get<StorageBufferProperty>( "Materials" ) )
            sb->SetRawData( &gpuMaterial, sizeof( gpuMaterial ) );

        if ( m_MaterialExecutor )
        {
            glm::mat4 transform = info.MeshTransform;
            m_MaterialExecutor->PushConstant( &transform, sizeof( glm::mat4 ), 0 );
            const uint32_t materialIndex = 0;
            m_MaterialExecutor->PushConstant( &materialIndex, sizeof( uint32_t ), sizeof( glm::mat4 ) );
        }

        Material::Bind( info.instance );
    }

    void SkinnedMaterialPBR::UpdateSkinnedUB( MaterialInstance* instance, const ShaderProtocols::SkinnedUB& skinnedUB )
    {
        static ShaderProtocols::SkinnedUB SkinnedUB;
        SkinnedUB.BoneMatrices = skinnedUB.BoneMatrices;

        if ( auto* prop = Get<StorageBufferProperty>( SkinnedUB.Name ) )
            prop->SetRawData( SkinnedUB.BoneMatrices.data(),
                              sizeof( glm::mat4 ) * SkinnedUB.BoneMatrices.size() );
    }

} // namespace Desert::Graphic
