#include "ResourceRegistry.hpp"

#include <Engine/Graphic/Geometry/MeshFactory.hpp>

namespace Desert::Runtime
{
    void ResourceRegistry::RegisterTexture( const Assets::AssetHandle&          handle,
                                            std::shared_ptr<Graphic::Texture2D> texture )
    {
        m_Textures[handle] = std::move( texture );
    }

    Common::BoolResult
    ResourceRegistry::RegisterSkybox( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAssetAsset )
    {
        if ( !skyboxAssetAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Skybox asset is invalid" );
        }

        m_Skyboxes[skyboxAssetAsset->GetMetadata().Handle] =
             std::make_shared<Graphic::MaterialSkybox>( skyboxAssetAsset );

        return BOOLSUCCESS;
    }

    std::shared_ptr<Desert::Graphic::Texture2D>
    ResourceRegistry::GetTexture( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Textures.find( handle );
        return ( it != m_Textures.end() ) ? it->second : nullptr;
    }

    std::optional<std::shared_ptr<Graphic::MaterialSkybox>> ResourceRegistry::GetSkybox( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Skyboxes.find( handle );
        return ( it != m_Skyboxes.end() ) ? std::make_optional( it->second ) : std::nullopt;
    }

    void ResourceRegistry::Clear()
    {
    }

    Common::BoolResult ResourceRegistry::RegisterMesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset )
    {
        if ( !meshAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Mesh asset is invalid" );
        }

        m_Meshes[meshAsset->GetMetadata().Handle] = Graphic::MeshFactory::Create( meshAsset );

        return BOOLSUCCESS;
    }

    std::shared_ptr<Desert::Mesh> ResourceRegistry::GetMesh( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Meshes.find( handle );
        return ( it != m_Meshes.end() ) ? it->second : nullptr;
    }
} // namespace Desert::Runtime