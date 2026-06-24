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

    void MaterialFactory::ApplyPBRAsset( StaticMaterialPBR& material, const Assets::PBRMaterialAsset& asset )
    {
        // The reflected data IS the material parameters — copy it wholesale (color, metallic, roughness,
        // AO, emissive, texture handles). No per-parameter setters.
        material.Data() = asset.Data();

        // Resolve texture handles to images and (re)bind them to the shader's sampler slots.
        auto resolveImage = []( Assets::AssetHandle handle ) -> Graphic::Image2D*
        {
            auto* tex = Runtime::ResourceRegistry::GetTextureService()->Get( handle );
            if ( !tex )
                return nullptr;
            return static_cast<Graphic::Image2D*>(
                Runtime::ResourceRegistry::GetImageService()->Resolve( tex->GetImageHandle() ) );
        };

        auto bindTexture = [&]( const Assets::AssetHandle& handle, const char* shaderName )
        {
            if ( static_cast<uint64_t>( handle ) == 0 )
                return;
            if ( auto* img = resolveImage( handle ) )
                if ( auto* prop = material.Get<Texture2DProperty>( shaderName ) )
                    prop->SetImage( img );
        };

        bindTexture( material.Data().AlbedoTexture, "u_AlbedoTexture" );
        bindTexture( material.Data().NormalTexture, "u_NormalTexture" );
    }

    std::shared_ptr<Material> MaterialFactory::CreateMaterial( const Assets::MaterialAsset* asset )
    {
        if ( !asset )
            return nullptr;

        switch ( asset->GetMaterialType() )
        {
            case Assets::MaterialAsset::MaterialType::PBR:
            {
                auto pbrMaterial = std::make_shared<StaticMaterialPBR>();
                ApplyPBRAsset( *pbrMaterial, *static_cast<const Assets::PBRMaterialAsset*>( asset ) );
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

        // Material parameters (and textures) are already taken from the asset's reflected data in
        // CreateMaterial(); the instance simply references that material.
        return material->CreateInstance( instanceName );
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
        auto pbrMaterial = std::make_shared<StaticMaterialPBR>();
        pbrMaterial->Data().AlbedoColor     = glm::vec4( color, 1.0f );
        pbrMaterial->Data().MetallicFactor  = metallic;
        pbrMaterial->Data().RoughnessFactor = roughness;

        return pbrMaterial->CreateInstance( name );
    }
} // namespace Desert::Graphic