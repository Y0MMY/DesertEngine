#include "MaterialPBRBase.hpp"

#include <Engine/Assets/Mapper.hpp>

#include <Engine/Graphic/ShaderProtocols/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/Metadata.hpp>

namespace Desert::Graphic
{

    MaterialPBRBase::MaterialPBRBase( const PBRMaterialData& data ) : m_RuntimeData( data )
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
        auto resolveTexture = [&]( Assets::TextureAsset::Type type ) -> Image2D*
        {
            // 1. runtime override
            switch ( type )
            {
                case Assets::TextureAsset::Type::Albedo:
                    return m_RuntimeData.Albedo;
                case Assets::TextureAsset::Type::Normal:
                    return m_RuntimeData.Normal;
                case Assets::TextureAsset::Type::Metallic:
                    return m_RuntimeData.Metallic;
                case Assets::TextureAsset::Type::Roughness:
                    return m_RuntimeData.Roughness;
                case Assets::TextureAsset::Type::AO:
                    return m_RuntimeData.AO;
                case Assets::TextureAsset::Type::Emissive:
                    return m_RuntimeData.Emissive;
            }

            
            return nullptr;
        };

        auto bind = [&]( Assets::TextureAsset::Type type, const std::string& name )
        {
            auto tex = resolveTexture( type );
            if ( !tex )
                return;

            if ( auto prop = executor->GetTexture2DProperty( name ) )
            {
                prop->SetImage( tex );
            }
        };

        bind( Assets::TextureAsset::Type::Albedo, "u_AlbedoTexture" );
        bind( Assets::TextureAsset::Type::Normal, "u_NormalTexture" );
        bind( Assets::TextureAsset::Type::Metallic, "metallicMap" );
        bind( Assets::TextureAsset::Type::Roughness, "roughnessMap" );
        bind( Assets::TextureAsset::Type::AO, "aoMap" );
        bind( Assets::TextureAsset::Type::Emissive, "emissiveMap" );
    }

} // namespace Desert::Graphic
