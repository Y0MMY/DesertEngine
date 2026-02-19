#include "MaterialPBRBase.hpp"

#include <Engine/Assets/Mapper.hpp>

#include <Engine/Graphic/ShaderProtocols/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/Metadata.hpp>

namespace Desert::Graphic
{

    MaterialPBRBase::MaterialPBRBase( const std::shared_ptr<Assets::MaterialAsset>& asset )
         : m_BaseMaterial( asset )
    {
    }

    void MaterialPBRBase::UpdateCamera( Material& material, const Core::Camera* camera )
    {
        static ShaderProtocols::Camera CameraUB;
        CameraUB.View       = camera->GetViewMatrix();
        CameraUB.Projection = camera->GetProjectionMatrix();
        CameraUB.CameraPos  = camera->GetPosition();

        material.Get<UniformBufferProperty>( CameraUB.Name )
             ->SetRawData( (std::byte*)&CameraUB, sizeof( CameraUB ) );
    }

    void MaterialPBRBase::UpdatePointLights( Material& material, const ShaderProtocols::PointLight& lights )
    {
        if ( lights.PointLights.empty() )
        {
            return;
        }
        material.Get<UniformBufferProperty>( lights.Name )
             ->SetRawData( (std::byte*)lights.PointLights.data(),
                           lights.PointLights.size() * sizeof( ShaderProtocols::PointLightPayload ) );
    }

    void MaterialPBRBase::UpdateDirectionLights( Material&                              material,
                                                 const ShaderProtocols::DirectionLight& lights )
    {
        if ( lights.DirectionLights.empty() )
        {
            return;
        }
        material.Get<UniformBufferProperty>( lights.Name )
             ->SetRawData( (std::byte*)lights.DirectionLights.data(),
                           lights.DirectionLights.size() * sizeof( ShaderProtocols::DirectionLightPayload ) );
    }

    void MaterialPBRBase::UpdateLightsMetadata( Material& material, const ShaderProtocols::PointLight& point,
                                                const ShaderProtocols::DirectionLight& dir )
    {
        static ShaderProtocols::LightsMetadata LightsMetadataUB;

        LightsMetadataUB.DirectionLightsCount = dir.DirectionLights.size();
        LightsMetadataUB.PointLightsCount     = point.PointLights.size();

        material.Get<UniformBufferProperty>( LightsMetadataUB.Name )
             ->SetRawData( (std::byte*)&LightsMetadataUB, sizeof( LightsMetadataUB ) );
    }

    void MaterialPBRBase::UpdatePBRTextures( Material& material, const ShaderProtocols::PBRTexturesUB& textures )
    {
        /* material.SetBufferValue( textures ? *textures : Models::PBR::PBRTextures{} );
         material.SyncToGPU<Models::PBR::PBRTextures>();*/
    }

    void MaterialPBRBase::UpdatePBRMaterial( Material&                                  material,
                                             const ShaderProtocols::PBRMeshMaterialsUB& meshUB )
    {
        /*material.SetBufferValue( *m_MaterialProperties );
        material.SyncToGPU<Models::PBR::PBRMaterialPropertiesUB>();*/
    }

    void MaterialPBRBase::UpdateTextures( const MaterialExecutor* executor )
    {

        auto GetFinalTexture = [&]( Assets::TextureAsset::Type type )
        {
            const auto& baseMaterial = m_BaseMaterial.lock();
            // Then check base material
            if ( baseMaterial )
            {
                return baseMaterial->GetTexture( type );
            }

            return (Assets::TextureAsset*)nullptr;
        };

        auto updateTexture = [&]( Assets::TextureAsset::Type type, const std::string& name )
        {
            auto texture = GetFinalTexture( type );
            {
                if ( auto texProp = executor->GetTexture2DProperty( name ) )
                {
                    // texProp->SetImage( texture-> );
                }
            }
        };

        updateTexture( Assets::TextureAsset::Type::Albedo, "u_AlbedoTexture" );
        updateTexture( Assets::TextureAsset::Type::Normal, "u_NormalTexture" );
        updateTexture( Assets::TextureAsset::Type::Metallic, "metallicMap" );
        updateTexture( Assets::TextureAsset::Type::Roughness, "roughnessMap" );
        updateTexture( Assets::TextureAsset::Type::AO, "aoMap" );
        updateTexture( Assets::TextureAsset::Type::Emissive, "emissiveMap" );
    }

} // namespace Desert::Graphic
