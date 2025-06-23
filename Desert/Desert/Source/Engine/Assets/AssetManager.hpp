#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <functional> // For std::hash

namespace Desert::Assets
{
    class AssetManager final
    {
    public:
        using KeyHandle      = std::pair<AssetTypeID, Common::Filepath>;
        using AssetContainer = std::vector<std::pair<KeyHandle, Asset<AssetBase>>>;
        using AssetIterator  = typename AssetContainer::iterator;

        struct KeyHandleHash
        {
            size_t operator()( const KeyHandle& key ) const
            {
                size_t h1 = std::hash<AssetTypeID>{}( key.first );
                size_t h2 = std::hash<Common::Filepath>{}( key.second );

                return h1 ^ ( h2 << 1 );
            }
        };

        struct KeyHandleEqual
        {
            bool operator()( const KeyHandle& lhs, const KeyHandle& rhs ) const
            {
                return lhs.first == rhs.first && lhs.second == rhs.second;
            }
        };

    public:
        template <typename AssetType>
        Asset<AssetType> CreateAsset( const AssetPriority priority, const Common::Filepath& filepath )
        {
            static_assert( std::is_base_of_v<AssetBase, AssetType>, "AssetType must inherit from AssetBase" );
            KeyHandle key{ AssetType::GetTypeID(), filepath };

            auto it = m_AssetLookup.find( key );
            if ( it != m_AssetLookup.end() )
            {
                return sp_cast<AssetType>( it->second->second );
            }

            auto asset = std::make_shared<AssetType>( priority, filepath );
            m_AssetsCache.emplace_back( key, asset );
            m_AssetLookup[key]                 = std::prev( m_AssetsCache.end() );
            m_HandleLookup[asset->GetHandle()] = asset;

            return asset;
        }

        Asset<AssetBase> FindByFilepath( const Common::Filepath& key, AssetTypeID typeID )
        {
            const KeyHandle handle = { typeID, key };
            auto      it     = m_AssetLookup.find( handle );
            return ( it != m_AssetLookup.end() ) ? it->second->second : nullptr;
        }

        template <typename TypeAsset>
        Asset<TypeAsset> FindByHandle( const AssetHandle& handle ) const
        {
            if ( auto it = m_HandleLookup.find( handle ); it != m_HandleLookup.end() )
            {
                return sp_cast<TypeAsset>(it->second);
            }
            return nullptr;
        }

    private:
        AssetContainer                                                              m_AssetsCache;
        std::unordered_map<KeyHandle, AssetIterator, KeyHandleHash, KeyHandleEqual> m_AssetLookup;
        std::unordered_map<AssetHandle, Asset<AssetBase>>                           m_HandleLookup;
    };
} // namespace Desert::Assets