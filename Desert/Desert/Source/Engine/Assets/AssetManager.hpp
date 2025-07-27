#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Assets
{
    class AssetManager final
    {
    public:
        using KeyHandle      = Common::Filepath;
        using AssetContainer = std::vector<std::pair<AssetMetadata, Asset<AssetBase>>>;
        using AssetIndex     = uint32_t;

        template <typename AssetType, typename... Args>
        Asset<AssetType> CreateAsset( const AssetPriority priority, const Common::Filepath& filepath,
                                      bool loadAfterCreate = true, Args&&... args )
        {
            static_assert( std::is_base_of_v<AssetBase, AssetType>, "AssetType must inherit from AssetBase" );

            AssetMetadata lookUpMetadata;
            lookUpMetadata.Filepath  = filepath;
            lookUpMetadata.AssetType = AssetType::GetTypeID();

            auto it = std::find_if( m_AssetsCache.begin(), m_AssetsCache.end(),
                                    [&lookUpMetadata]( const auto& assetCache )
                                    { return assetCache.first.IsEquivalent( lookUpMetadata ); } );

            if ( it != m_AssetsCache.end() )
            {
                return sp_cast<AssetType>( it->second );
            }

            // NOTE:Perhaps the creation of an asset via the Create() method should be defined for each type
            // separately, and then call AssetType::Create()
            auto asset = std::make_shared<AssetType>( priority, filepath, std::forward<Args>( args )... );
            if ( loadAfterCreate )
            {
                asset->Load();
            }

            const auto& metadata = asset->GetMetadata();
            m_AssetsCache.push_back( { metadata, asset } );
            m_HandleLookup[metadata.Handle] = m_AssetsCache.size() - 1;
            return asset;
        }

        template <typename TypeAsset>
        Asset<TypeAsset> FindByHandle( const AssetHandle& handle ) const
        {
            if ( auto it = m_HandleLookup.find( handle ); it != m_HandleLookup.end() )
            {
                return sp_cast<TypeAsset>( m_AssetsCache[it->second].second );
            }
            return nullptr;
        }

        template <typename TypeAsset>
        std::vector<std::pair<AssetHandle, Asset<TypeAsset>>> FindAllByType() const
        {
            std::vector<std::pair<AssetHandle, Asset<TypeAsset>>> result;
            const auto                                            typeId = TypeAsset::GetTypeID();

            for ( const auto& [metadata, asset] : m_AssetsCache )
            {
                if ( metadata.AssetType == typeId )
                {
                    if ( auto casted = sp_cast<TypeAsset>( asset ) )
                    {
                        result.emplace_back( metadata.Handle, casted );
                    }
                }
            }

            return result;
        }

    private:
        AssetContainer                              m_AssetsCache;
        std::unordered_map<AssetHandle, AssetIndex> m_HandleLookup;
    };
} // namespace Desert::Assets