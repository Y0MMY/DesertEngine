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
                return AsRequestedType<AssetType>( m_AssetsCache[it->second].second, "CreateAsset", cacheKey );
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
                return AsRequestedType<TypeAsset>( m_AssetsCache[it->second].second, "FindByHandle",
                                                   std::to_string( static_cast<uint64_t>( handle ) ) );
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
                return AsRequestedType<TypeAsset>( it->second, "FindByPath", path.generic_string() );
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
                    // Filtered on the REGISTRY's copy of the type, then checked against the ASSET's own:
                    // the two are written at different moments and a census that trusted only the copy
                    // would count a record the copy mislabels. AsRequestedType refuses and says so.
                    if ( auto casted = AsRequestedType<TypeAsset>( asset, "FindAllByType",
                                                                   metadata.Filepath.generic_string() ) )
                    {
                        result.emplace_back( metadata.Handle, casted );
                    }
                }
            }

            return result;
        }

    private:
        // A TYPED LOOKUP ANSWERS OR REFUSES. It never reinterprets.
        //
        // Every Find* above used to end in `sp_cast`, i.e. `std::static_pointer_cast`, and that is not a
        // question — it is an ASSERTION that the record holds the requested class. When the assertion was
        // wrong the caller got a perfectly non-null pointer to an object of another class, so every `if
        // (!asset)` downstream waved it through and the code read a stranger's memory as its own. It was
        // observed: a `SkyboxAsset` came back from a `CloudTypeAsset` request, non-null, in the
        // AssetHandleStability suite. A wrong answer that is indistinguishable from a right one is the
        // worst thing a lookup can return, and no caller can defend against it.
        //
        // Today's key carries the type, so nothing collides today. This exists because the FAILURE MODE,
        // not the collision, is the defect: any future collision — a hash meeting, an id read out of a
        // file, an importer key too coarse to separate two assets — would again produce a plausible
        // pointer instead of an error.
        //
        // The check needs no RTTI. `AssetBase` stamps its own `AssetTypeID` at construction and
        // `GetMetadata()` is `final`, so a record can be ASKED what it is: one integer compare against
        // `TypeAsset::GetTypeID()`. `dynamic_pointer_cast` answers the same question and was measured
        // rather than guessed, because this sits on the preload path where the neighbouring lookup already
        // cost 56.9 s on 2000 assets when it was written the naive way — see the developer's report for
        // the numbers.
        //
        // The mismatch is NAMED, not merely turned into null: `who` is the entry point and `subject` the
        // key that was asked about, so the log line says which question produced a stranger and which
        // record the stranger was.
        template <typename TypeAsset>
        static Asset<TypeAsset> AsRequestedType( const Asset<AssetBase>& stored, const char* who,
                                                 const std::string& subject )
        {
            static_assert( std::is_base_of_v<AssetBase, TypeAsset>, "TypeAsset must inherit from AssetBase" );

            if ( !stored )
            {
                return nullptr;
            }

            const AssetTypeID wanted = TypeAsset::GetTypeID();
            const AssetTypeID actual = stored->GetMetadata().AssetType;
            if ( actual != wanted )
            {
                LOG_ERROR( "AssetManager::{}: '{}' holds a {} asset (type id {}) but was requested as {} "
                           "(type id {}). Refusing to reinterpret it; returning null.",
                           who, subject, AssetTypeName( actual ), static_cast<int>( actual ),
                           AssetTypeName( wanted ), static_cast<int>( wanted ) );
                return nullptr;
            }

            return sp_cast<TypeAsset>( stored );
        }

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