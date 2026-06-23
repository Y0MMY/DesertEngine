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

                // Override typed TProperty defaults from the asset
                if ( auto color = pbrAsset->GetAlbedoColor() )
                    pbrMaterial->SetAlbedoColor( *color );

                if ( auto metallic = pbrAsset->GetMetallicFactor() )
                    pbrMaterial->SetMetallicValue( *metallic );

                if ( auto roughness = pbrAsset->GetRoughnessFactor() )
                    pbrMaterial->SetRoughnessValue( *roughness );

                // Resolve and upload textures from TextureService so all instances inherit them
                auto resolveImage = []( Assets::AssetHandle handle ) -> Graphic::Image2D*
                {
                    auto* tex = Runtime::ResourceRegistry::GetTextureService()->Get( handle );
                    if ( !tex )
                        return nullptr;
                    return static_cast<Graphic::Image2D*>(
                        Runtime::ResourceRegistry::GetImageService()->Resolve( tex->GetImageHandle() ) );
                };

                if ( auto h = pbrAsset->GetTextureHandle( Assets::TextureAsset::Type::Albedo ) )
                {
                    if ( auto* img = resolveImage( *h ) )
                        pbrMaterial->SetAlbedoTexture( img );
                }

                if ( auto h = pbrAsset->GetTextureHandle( Assets::TextureAsset::Type::Normal ) )
                {
                    if ( auto* img = resolveImage( *h ) )
                        pbrMaterial->SetNormalTexture( img );
                }

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

            // Per-instance scalar overrides — names must match TProperty shader names
            if ( auto metallic = pbrAsset->GetMetallicFactor() )
                instance->SetFloat( "MetallicValue", *metallic );

            if ( auto roughness = pbrAsset->GetRoughnessFactor() )
                instance->SetFloat( "RoughnessValue", *roughness );

            if ( auto color = pbrAsset->GetAlbedoColor() )
                instance->SetVec3( "AlbedoColor", *color );
        }

        return instance;
    }

    std::shared_ptr<Material> MaterialFactory::CreateDefaultPBRMaterial()
    {
        // MPROPERTY defaults in StaticMaterialPBR constructor handle initialization.
        return std::make_shared<StaticMaterialPBR>();
    }

    std::shared_ptr<MaterialInstance> MaterialFactory::CreatePrimitiveMaterial( const std::string& name,
                                                                                const glm::vec3&   color,
                                                                                float metallic, float roughness )
    {
        auto pbrMaterial = CreateDefaultPBRMaterial();
        auto instance    = pbrMaterial->CreateInstance( name );

        instance->SetVec3( "AlbedoColor", color );
        instance->SetFloat( "MetallicValue", metallic );
        instance->SetFloat( "RoughnessValue", roughness );

        return instance;
    }
} // namespace Desert::Graphic