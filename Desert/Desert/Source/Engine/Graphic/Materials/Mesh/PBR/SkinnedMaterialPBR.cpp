#include "SkinnedMaterialPBR.hpp"

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic
{
    void SkinnedMaterialPBR::Bind( const UpdateSkinnedMaterialPBRInfo& info )
    {
        // The whole scene contribution in ONE call, through the SAME applier and from the SAME snapshot
        // the static meshes are lit by. What used to stand here was a hand-picked list of four uploads;
        // the cascades and the environment cubes were not on it, so a skinned mesh was the one class of
        // geometry in the engine that received neither.
        info.Scene.ApplyTo( info.instance );

        UpdateSkinnedUB( info.SkinnedUB );

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

    void SkinnedMaterialPBR::UpdateSkinnedUB( const ShaderProtocols::SkinnedUB& skinnedUB )
    {
        if ( auto* prop = Get<StorageBufferProperty>( ShaderProtocols::SkinnedUB::Name ) )
            prop->SetRawData( skinnedUB.BoneMatrices.data(),
                              static_cast<uint32_t>( sizeof( glm::mat4 ) * skinnedUB.BoneMatrices.size() ) );
    }

} // namespace Desert::Graphic
