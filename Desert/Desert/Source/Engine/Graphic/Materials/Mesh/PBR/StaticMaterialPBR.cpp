#include "StaticMaterialPBR.hpp"
#include "MaterialPBRBase.hpp"
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic
{
    StaticMaterialPBR::StaticMaterialPBR() : Material( "PBRMaterial", "StaticMeshPBR" )
    {
    }

    StaticMaterialPBR::StaticMaterialPBR( std::string&& debugName, std::string&& shaderName )
         : Material( std::move( debugName ), std::move( shaderName ) )
    {
    }

    StaticMaterialPBRInstanced::StaticMaterialPBRInstanced()
         : StaticMaterialPBR( "PBRMaterialInstanced", "StaticMeshPBR_Instanced" )
    {
    }

    void StaticMaterialPBR::UpdateTransform( MaterialInstance* instance, const glm::mat4& transform )
    {
        instance->SetMat4( "Transform", transform );
    }

    void StaticMaterialPBR::Bind( const MaterialInstance* instance )
    {
        if ( !m_MaterialExecutor )
            return;

        // Transform sub-block at offset 0 (RenderMesh overwrites per submesh); per-object material
        // index after it. The material data itself lives in the Materials[] storage buffer (filled by
        // MeshRenderer::DrawStaticMeshes).
        glm::mat4 transform = instance->GetMat4( "Transform" );
        m_MaterialExecutor->PushConstant( &transform, sizeof( glm::mat4 ), 0 );
        m_MaterialExecutor->PushConstant( &m_MaterialIndex, sizeof( uint32_t ), sizeof( glm::mat4 ) );

        // Flush shared uniform buffers (camera/lights), textures and the Materials storage descriptor.
        Material::Bind( instance );
    }

    void StaticMaterialPBR::OnBind( MaterialInstance* instance )
    {
    }
} // namespace Desert::Graphic
