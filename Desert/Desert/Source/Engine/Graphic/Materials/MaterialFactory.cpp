#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <Engine/Assets/Mapper.hpp>

#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp>

#include <Engine/Assets/Mesh/PBRMaterialAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic
{
    // std::shared_ptr<Material> MaterialFactory::Create( const std::shared_ptr<Assets::MaterialAsset>& asset )
    //{
    //     switch ( asset->GetMaterialType() )
    //     {
    //         case Assets::MaterialAsset::MaterialType::PBR:
    //         {
    //             const auto                       pbrAsset = static_cast<Assets::PBRMaterialAsset*>( asset.get()
    //             ); MaterialPBRBase::PBRMaterialData data; const auto handleAlbedo = pbrAsset->GetTextureHandle(
    //             Assets::TextureAsset::Type::Albedo ); if ( handleAlbedo.has_value() )
    //             {
    //                 const auto texture =
    //                      Runtime::ResourceRegistry::GetTextureService()->Get( handleAlbedo.value() );
    //                 if ( texture != nullptr )
    //                 {
    //                     const auto imageHandle = texture->GetImageHandle();

    //                    data.Albedo = static_cast<Image2D*>(
    //                         Runtime::ResourceRegistry::GetImageService()->Resolve( imageHandle ) );
    //                }
    //            }
    //            return std::make_shared<StaticMaterialPBR>( data );
    //        }

    //        case Assets::MaterialAsset::MaterialType::Skybox:
    //        {
    //            //  return std::make_shared<MaterialSkybox>( asset );
    //        }

    //        default:
    //        {
    //            return nullptr; // CreateDefault();
    //        }
    //    }

    //    return nullptr;
    //}

    std::shared_ptr<Material> MaterialFactory::CreateMaterial( const Assets::MaterialAsset* asset )
    {
        if ( !asset )
            return nullptr;

        switch ( asset->GetMaterialType() )
        {
            case Assets::MaterialAsset::MaterialType::PBR:
            {
                auto pbrMaterial = std::make_shared<StaticMaterialPBR>();

                auto pbrAsset = static_cast<const Assets::PBRMaterialAsset*>( asset );

                // Set default parameters from asset (only scalar values, not textures)
                if ( auto color = pbrAsset->GetAlbedoColor() )
                    pbrMaterial->SetDefaultParameter( "AlbedoColor", *color, MaterialPropertyType::Vec3 );

                if ( auto metallic = pbrAsset->GetMetallicFactor() )
                    pbrMaterial->SetDefaultParameter( "MetallicFactor", *metallic, MaterialPropertyType::Float );

                if ( auto roughness = pbrAsset->GetRoughnessFactor() )
                    pbrMaterial->SetDefaultParameter( "RoughnessFactor", *roughness, MaterialPropertyType::Float );

                return pbrMaterial;
            }

            default:
                return CreateDefaultPBRMaterial();
        }
    }

    std::shared_ptr<MaterialInstance> MaterialFactory::CreateMaterialInstance( const Assets::MaterialAsset* asset,
                                                                               const std::string& instanceName )
    {
        auto material = CreateMaterial( asset );
        if ( !material )
            return nullptr;

        auto instance = material->CreateInstance( instanceName );

        if ( asset->GetMaterialType() == Assets::MaterialAsset::MaterialType::PBR )
        {
            auto pbrAsset = static_cast<const Assets::PBRMaterialAsset*>( asset );

            // Resolve and set textures using the resolver
            if ( auto albedoHandle = pbrAsset->GetTextureHandle( Assets::TextureAsset::Type::Albedo ) )
            {
                /* if ( auto image = m_Resolver->ResolveTexture2D( *albedoHandle ) )
                 {
                     instance->SetTexture( "AlbedoTexture", const_cast<Image2D*>( image ) );
                     instance->SetInt( "UseAlbedoTexture", 1 );
                 }*/
            }

            if ( auto normalHandle = pbrAsset->GetTextureHandle( Assets::TextureAsset::Type::Normal ) )
            {
                /*if ( auto image = m_Resolver->ResolveTexture2D( *normalHandle ) )
                {
                    instance->SetTexture( "NormalTexture", const_cast<Image2D*>( image ) );
                    instance->SetInt( "UseNormalTexture", 1 );
                }*/
            }

            // Set scalar values
            if ( auto metallic = pbrAsset->GetMetallicFactor() )
                instance->SetFloat( "MetallicFactor", *metallic );

            if ( auto roughness = pbrAsset->GetRoughnessFactor() )
                instance->SetFloat( "RoughnessFactor", *roughness );

            if ( auto color = pbrAsset->GetAlbedoColor() )
                instance->SetVec3( "AlbedoColor", *color );
        }

        return instance;
    }

    std::shared_ptr<Material> MaterialFactory::CreateDefaultPBRMaterial()
    {
        auto material = std::make_shared<StaticMaterialPBR>();

        material->SetDefaultParameter( "AlbedoColor", glm::vec3( 0.8f, 0.8f, 0.8f ), MaterialPropertyType::Vec3 );
        material->SetDefaultParameter( "MetallicFactor", 0.0f, MaterialPropertyType::Float );
        material->SetDefaultParameter( "RoughnessFactor", 0.5f, MaterialPropertyType::Float );
        material->SetDefaultParameter( "UseAlbedoTexture", 0, MaterialPropertyType::Int );
        material->SetDefaultParameter( "UseNormalTexture", 0, MaterialPropertyType::Int );

        return material;
    }

    std::shared_ptr<MaterialInstance> MaterialFactory::CreatePrimitiveMaterial( const std::string& name,
                                                                                const glm::vec3&   color,
                                                                                float metallic, float roughness )
    {
        auto pbrMaterial = CreateDefaultPBRMaterial();
        auto instance    = pbrMaterial->CreateInstance( name );

        instance->SetVec3( "AlbedoColor", color );
        instance->SetFloat( "MetallicFactor", metallic );
        instance->SetFloat( "RoughnessFactor", roughness );
        instance->SetInt( "UseAlbedoTexture", 0 );

        return instance;
    }
} // namespace Desert::Graphic