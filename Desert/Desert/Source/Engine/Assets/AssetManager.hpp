#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Assets
{
    class AssetManager final
    {
    public:
        using KeyHandle      = Common::Filepath;
        using AssetContainer = std::vector<std::pair<KeyHandle, Asset<AssetBase>>>;
        using AssetIterator  = uint32_t;

        template <typename AssetType, typename... Args>
        Asset<AssetType> CreateAsset( const AssetPriority priority, const Common::Filepath& filepath,
                                      bool loadAfterCreate = true, Args&&... args)
        {
            static_assert( std::is_base_of_v<AssetBase, AssetType>, "AssetType must inherit from AssetBase" );

            const auto& key = AssetType::GetAssetKey( filepath );

            if ( auto it = m_AssetLookup.find( key ); it != m_AssetLookup.end() )
            {
                return sp_cast<AssetType>( m_AssetsCache[it->second].second );
            }

            // NOTE:Perhaps the creation of an asset via the Create() method should be defined for each type
            // separately, and then call AssetType::Create()
            auto asset = std::make_shared<AssetType>( priority, filepath, std::forward<Args>( args )... );
            if ( loadAfterCreate )
            {
                asset->Load();
            }

            m_AssetsCache.push_back( { key, asset } );
            m_AssetLookup[key]                 = m_AssetsCache.size() - 1;
            m_HandleLookup[asset->GetHandle()] = asset;
            return asset;
        }

        Asset<AssetBase> FindByFilepath( const KeyHandle& handle, AssetTypeID typeID )
        {
            auto it = m_AssetLookup.find( handle );
            return ( it != m_AssetLookup.end() ) ? m_AssetsCache[it->second].second : nullptr;
        }

        template <typename TypeAsset>
        Asset<TypeAsset> FindByHandle( const AssetHandle& handle ) const
        {
            if ( auto it = m_HandleLookup.find( handle ); it != m_HandleLookup.end() )
            {
                return sp_cast<TypeAsset>( it->second );
            }
            return nullptr;
        }

    private:
        AssetContainer                                    m_AssetsCache;
        std::unordered_map<KeyHandle, AssetIterator>      m_AssetLookup;
        std::unordered_map<AssetHandle, Asset<AssetBase>> m_HandleLookup;
    };
} // namespace Desert::Assets