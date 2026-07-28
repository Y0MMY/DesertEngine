#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <Engine/Assets/Mapper.hpp>

#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp>
#include <Engine/Graphic/Materials/DataDrivenMaterial.hpp>

#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Logger.hpp>

namespace Desert::Graphic
{
    void MaterialFactory::ApplyPBRAsset( StaticMaterialPBR& material, const Assets::SurfaceMaterialAsset& asset )
    {
        // Build the backend's typed view from the material canon (single protocol -> optimized
        // hot-path struct). No per-parameter setters.
        material.Data() = Assets::PBRSurfaceParams::FromMaterialData( asset.Data() );

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
            auto* img = resolveImage( handle );
            LOG_INFO( "[Mat][Bind] {} handle={} resolved={}", shaderName, static_cast<uint64_t>( handle ),
                      img != nullptr );
            if ( img )
                if ( auto* prop = material.Get<Texture2DProperty>( shaderName ) )
                    prop->SetImage( img );
        };

        bindTexture( material.Data().AlbedoTexture, "u_AlbedoTexture" );
        bindTexture( material.Data().NormalTexture, "u_NormalTexture" );
        bindTexture( material.Data().OpacityTexture, "u_OpacityTexture" );
    }

    void MaterialFactory::ApplyShaderAsset( DataDrivenMaterial& material, const Assets::SurfaceMaterialAsset& asset )
    {
        // Seed schema defaults, then overlay the asset's persisted parameter values.
        material.ApplyDefaults();

        const auto& data = asset.Data();
        for ( const auto& p : data.Params )
            material.SetParamRaw( p.Name, p.Value );

        for ( const auto& t : data.Textures )
        {
            if ( t.TextureHandle == 0 )
                continue;
            auto* tex = Runtime::ResourceRegistry::GetTextureService()->Get( Common::UUID( t.TextureHandle ) );
            if ( !tex )
                continue;
            auto* img = static_cast<Graphic::Image2D*>(
                 Runtime::ResourceRegistry::GetImageService()->Resolve( tex->GetImageHandle() ) );
            if ( img )
                material.SetTexture( t.Name, img );
        }
    }

    std::shared_ptr<Material> MaterialFactory::CreateMaterial( const Assets::MaterialAsset* asset )
    {
        if ( !asset )
            return nullptr;

        const std::string shaderName = asset->GetShaderName();

        // Shader-name registry (replaces the old closed MaterialType switch). Specialized shaders keep
        // their optimized C++ material (PBR batches into an SSBO); any other shader is handled generically
        // by DataDrivenMaterial — so a new shader becomes assignable with zero C++.
        if ( shaderName.empty() || shaderName == "StaticMeshPBR" || shaderName == "SkinnedMeshPBR" )
        {
            auto pbrMaterial = std::make_shared<StaticMaterialPBR>();
            if ( const auto* pbr = dynamic_cast<const Assets::SurfaceMaterialAsset*>( asset ) )
                ApplyPBRAsset( *pbrMaterial, *pbr );
            return pbrMaterial;
        }

        auto ddm = std::make_shared<DataDrivenMaterial>( shaderName );
        if ( const auto* pbr = dynamic_cast<const Assets::SurfaceMaterialAsset*>( asset ) )
            ApplyShaderAsset( *ddm, *pbr );
        return ddm;
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