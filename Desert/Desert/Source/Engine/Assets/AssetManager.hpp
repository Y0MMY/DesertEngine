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

            // ONE lookup, on the asset's identity key, and the key is built once per registration.
            //
            // This used to be a linear scan comparing raw paths. Deduplicating on the identity key is
            // what stops a second SPELLING of an already-registered file becoming a second record that
            // then takes over the handle — but computing that key inside a scan is quadratic in the
            // number of assets AND in the work per comparison: measured over a preload of 2000 assets,
            // 56.9 s against 207 ms for the raw-path scan. Asking the question once and hashing it makes
            // the same preload 30 ms, i.e. faster than the version this replaces, because a string
            // compare beats a std::filesystem::path compare.
            AssetMetadata lookUpMetadata;
            lookUpMetadata.Filepath  = filepath;
            lookUpMetadata.AssetType = AssetType::GetTypeID();

            const std::string cacheKey = RegistryKey( lookUpMetadata );

            if ( const auto it = m_PathLookup.find( cacheKey ); it != m_PathLookup.end() )
            {
                return sp_cast<AssetType>( m_AssetsCache[it->second].second );
            }

            // NOTE:Perhaps the creation of an asset via the Create() method should be defined for each type
            // separately, and then call AssetType::Create()
            auto asset = std::make_shared<AssetType>( priority, filepath, std::forward<Args>( args )... );
            if ( loadAfterCreate )
            {
                const auto& loadResult = asset->Load();
                if ( !loadResult )
                {
                    LOG_ERROR( "Error while loading {}. Error: {}", filepath.string(), loadResult.GetError() );
                    return nullptr;
                }
            }

            const auto& metadata = asset->GetMetadata();
            m_AssetsCache.push_back( { metadata, asset } );
            m_HandleLookup[metadata.Handle] = m_AssetsCache.size() - 1;
            // Keyed on the LOOKUP's key, not on the stored metadata's: Texture2D and Material replace
            // their handle from an id inside the file during Load above, but their PATH is unchanged, so
            // both spellings resolve here either way. Using the record's own key would be the same
            // string; using the lookup's says plainly which question this map answers.
            m_PathLookup[cacheKey] = m_AssetsCache.size() - 1;

            if constexpr ( std::is_base_of_v<AssetBase, AssetType> )
            {
                asset->ResolveDependencies( *this );
            }

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
        Asset<TypeAsset> FindByPath( const Common::Filepath& path ) const
        {
            const auto typeId = TypeAsset::GetTypeID();
            auto       it     = std::find_if( m_AssetsCache.begin(), m_AssetsCache.end(),
                                              [&]( const auto& assetCache )
                                              {
                                                  return assetCache.first.AssetType == typeId &&
                                                         assetCache.first.Filepath == path;
                                              } );

            if ( it != m_AssetsCache.end() )
            {
                return sp_cast<TypeAsset>( it->second );
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
        // "Which file is this, and as what type?" — the asset's identity key with its type appended,
        // because two asset classes are allowed to sit on one path (the handle derivation deliberately
        // gives them the same number) and they are still two records.
        static std::string RegistryKey( const AssetMetadata& metadata )
        {
            return metadata.StableKey() + '#' + std::to_string( static_cast<int>( metadata.AssetType ) );
        }

        AssetContainer                              m_AssetsCache;
        std::unordered_map<AssetHandle, AssetIndex> m_HandleLookup;
        std::unordered_map<std::string, AssetIndex> m_PathLookup;
    };

    template <typename T>
    struct AssetDependency
    {
        AssetHandle      Handle;
        std::weak_ptr<T> Cached;

        void Resolve( AssetManager& manager )
        {
            auto asset = manager.FindByHandle<T>( Handle );
            Cached     = asset;
        }

        bool IsValid() const
        {
            return !Cached.expired();
        }

        T* Get() const
        {
            if (auto ptr = Cached.lock())
            {
                return ptr.get();
            }

            return nullptr;
        }
    };
} // namespace Desert::Assets