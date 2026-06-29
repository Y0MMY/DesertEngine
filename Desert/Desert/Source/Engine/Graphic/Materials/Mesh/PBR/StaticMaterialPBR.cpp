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

    void StaticMaterialPBR::UpdateCamera( MaterialInstance* instance, const Core::Camera* camera )
    {
        MaterialPBRBase::UpdateCamera( instance, camera );
    }

    void StaticMaterialPBR::UpdateLights( MaterialInstance* instance, const ShaderProtocols::PointLight& pointLights,
                                          const ShaderProtocols::SpotLight&      spotLights,
                                          const ShaderProtocols::DirectionLight& dirLights )
    {
        MaterialPBRBase::UpdateLights( instance, pointLights, spotLights, dirLights );
    }

    void StaticMaterialPBR::UpdateShadow( MaterialInstance* instance, const glm::mat4* cascadeViewProj,
                                          Image2D* const* cascadeMaps, uint32_t numCascades, float bias,
                                          bool enabled, int debugMode, bool showNormals,
                                          const glm::vec4& cascadeWorldPerTexel, bool lightingDebug )
    {
        MaterialPBRBase::UpdateShadow( instance, cascadeViewProj, cascadeMaps, numCascades, bias, enabled,
                                       debugMode, showNormals, cascadeWorldPerTexel, lightingDebug );
    }

    void StaticMaterialPBR::UpdateEnvironment( MaterialInstance* instance, ImageCube* irradiance,
                                               ImageCube* prefiltered, Image2D* brdfLut )
    {
        MaterialPBRBase::UpdateEnvironment( instance, irradiance, prefiltered, brdfLut );
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
