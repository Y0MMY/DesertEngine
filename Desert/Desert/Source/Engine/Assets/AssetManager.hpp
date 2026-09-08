#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>

#include <algorithm>
#include <typeinfo>
#include <vector>

namespace Desert::Assets
{
    class AssetManager final
    {
    public:
        /**
         * @brief EVERY REGISTRY THAT IS CURRENTLY ALIVE, in creation order.
         *
         * The same arrangement, for the same reason, as `Core::Scene::LiveScenes()`: asset eviction runs
         * from the engine's frame loop, which has no way to be handed a registry — the thing that OWNS one
         * is the editor or runtime layer, and both are above this layer. A registry that puts itself in a
         * list is the only shape in which "sweep the registries" is answerable from below.
         *
         * Raw pointers, non-owning, entered by the constructor and removed by the destructor, so an entry
         * can never outlive its object. There is one registry per project today; a project switch that
         * built a second before releasing the first would simply have both swept, which is correct.
         */
        //
        // HEADER-ONLY, AND THAT IS LOAD-BEARING RATHER THAN A STYLE CHOICE. This class had no translation
        // unit of its own, and eleven test suites rely on that: they compile a hand-picked list of asset
        // sources precisely so that a registry can be constructed without linking the renderer. Giving it
        // a `.cpp` broke every one of them with an undefined `AssetManager::AssetManager()` — caught by
        // the sweep, one commit after it was written. The list below stays inline for the same reason.
        [[nodiscard]] static std::vector<AssetManager*>& LiveManagerList()
        {
            static std::vector<AssetManager*> managers;
            return managers;
        }

        [[nodiscard]] static const std::vector<AssetManager*>& LiveManagers()
        {
            return LiveManagerList();
        }

        AssetManager()
        {
            LiveManagerList().push_back( this );
        }

        ~AssetManager()
        {
            auto& managers = LiveManagerList();
            managers.erase( std::remove( managers.begin(), managers.end(), this ), managers.end() );
        }

        // Deleted for the reason the list makes newly load-bearing: a copy would enter a second pointer to
        // one logical registry, a move would leave the moved-from husk in the list.
        AssetManager( const AssetManager& )            = delete;
        AssetManager& operator=( const AssetManager& ) = delete;
        AssetManager( AssetManager&& )                 = delete;
        AssetManager& operator=( AssetManager&& )      = delete;

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

        // "WHICH FILE IS THIS HANDLE?" — asked without claiming to know what type the handle names.
        //
        // Every lookup above is TYPED, and rightly so: a typed question that cannot be answered must be
        // refused rather than reinterpreted, which is what AsRequestedType is for. But that leaves one
        // question with no answer at all — a caller holding only a handle, for instance the editor
        // reporting that a document could NOT be opened, has no type to ask with and needs a name a person
        // recognises rather than a decimal id.
        //
        // Returning METADATA and not an asset is what makes this safe to answer untyped: there is nothing
        // here to cast, so the failure mode the typed lookups exist to prevent — a plausible pointer to an
        // object of another class — cannot occur. The metadata's own AssetType says what the record is,
        // and the caller may not assume anything else about it. Null for a handle that is not registered.
        [[nodiscard]] const AssetMetadata* FindMetadataByHandle( const AssetHandle& handle ) const
        {
            if ( auto it = m_HandleLookup.find( handle ); it != m_HandleLookup.end() )
                return &m_AssetsCache[it->second].first;
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
        // WHY `dynamic_pointer_cast` AND NOT THE STORED `AssetTypeID`. Comparing `TypeAsset::GetTypeID()`
        // against the record's own `AssetTypeID` is one integer compare and needs no RTTI, and it is not
        // enough: `AssetTypeID::Mesh` covers TWO C++ classes. `StaticMeshAsset` and `SkinnedMeshAsset` both
        // report `Mesh` — neither declares its own `GetTypeID()` — so an id compare passes a skinned record
        // to `FindByHandle<StaticMeshAsset>` and `static_pointer_cast` reinterprets it. That is reachable
        // from the editor, not hypothetical: the static mesh picker lists `FindAllByType<MeshAsset>()`,
        // which is every mesh including the skinned ones, and `StaticMeshComponent` then resolves the
        // chosen handle as `StaticMeshAsset`. An id compare would close the collision this task was given
        // and leave that one open.
        //
        // MEASURED, not estimated, because this sits on the preload path where the neighbouring lookup
        // already cost 56.9 s on 2000 assets when it was written the naive way. Over 1e6 lookups on the
        // real asset classes, Release, arm64: `static_pointer_cast` with no check 10.5 ns, id compare
        // 5.6 ns (cheaper than the baseline — the refusing half never constructs a shared_ptr),
        // `dynamic_pointer_cast` 15.0 ns. RTTI therefore costs ~4.5 ns per lookup, i.e. ~9 microseconds
        // over a 2000-asset preload, against a defect class that has already cost this programme days.
        // The id is kept for the MESSAGE, where naming the two asset types is what a human can act on.
        //
        // The mismatch is NAMED, not merely turned into null: `who` is the entry point and `subject` the
        // key that was asked about, so the log line says which question produced a stranger and which
        // record the stranger was. `typeid` is there for the case the two asset type NAMES are equal —
        // exactly the static-versus-skinned mesh above, where "Mesh was requested as Mesh" would tell the
        // reader nothing.
        //
        // AND THE SEVERITY IS DERIVED, not fixed: the same equal-names case is ALSO the case where the
        // miss is expected rather than wrong, so it is reported at trace. See the branch below — the
        // discriminator is whether the registry's recorded type id agrees with the requested one.
        template <typename TypeAsset>
        static Asset<TypeAsset> AsRequestedType( const Asset<AssetBase>& stored, const char* who,
                                                 const std::string& subject )
        {
            static_assert( std::is_base_of_v<AssetBase, TypeAsset>, "TypeAsset must inherit from AssetBase" );

            if ( !stored )
            {
                return nullptr;
            }

            auto typed = std::dynamic_pointer_cast<TypeAsset>( stored );
            if ( !typed )
            {
                // The dereference is bound to a reference first because `typeid` on an expression WITH
                // SIDE EFFECTS evaluates it, and `*stored` is `shared_ptr::operator*` — a function call,
                // so the operand is not the plain lvalue it reads as. Same dynamic type, intent stated.
                const AssetBase& storedRef = *stored;

                // TWO FAILURES WEAR ONE FACE HERE, AND ONLY ONE OF THEM IS A DEFECT.
                //
                // If the registry's recorded type id DISAGREES with the type asked for, the caller went
                // looking for a Material and found a Mesh: somebody is holding the wrong handle, or a
                // record is mislabelled. That is worth an ERROR and always was.
                //
                // If the two type ids AGREE and the cast still failed, nothing is wrong at all. Several
                // asset CLASSES share one type id — StaticMeshAsset and SkinnedMeshAsset are both
                // `Mesh` — so `FindAllByType<SkinnedMeshAsset>()` walks past every static mesh in the
                // project by design, and each miss used to produce a line reading "holds a Mesh asset but
                // was requested as Mesh". Two per scene load in this tree, at ERROR, saying what looks
                // like a typo. Real ERROR lines are only worth reading if they are all real; a routine
                // subtype probe is not one, so it goes to trace and NAMES BOTH CLASSES rather than the
                // shared type name that made the pair indistinguishable.
                const bool typeIdAgrees = stored->GetMetadata().AssetType == TypeAsset::GetTypeID();
                if ( typeIdAgrees )
                {
                    LOG_TRACE( "AssetManager::{}: '{}' is a '{}', not the '{}' this lookup asked for — both "
                               "are {} assets, so this is a subtype probe passing over it, not an error.",
                               who, subject, typeid( storedRef ).name(), typeid( TypeAsset ).name(),
                               AssetTypeName( TypeAsset::GetTypeID() ) );
                }
                else
                {
                    LOG_ERROR( "AssetManager::{}: '{}' holds a {} asset (type id {}, class '{}') but was "
                               "requested as {} (class '{}'). Refusing to reinterpret it; returning null.",
                               who, subject, AssetTypeName( stored->GetMetadata().AssetType ),
                               static_cast<int>( stored->GetMetadata().AssetType ), typeid( storedRef ).name(),
                               AssetTypeName( TypeAsset::GetTypeID() ), typeid( TypeAsset ).name() );
                }
            }

            return typed;
        }

    public:
        /**
         * @brief Every registered asset's METADATA, in registration order. Read-only, and metadata only.
         *
         * The typed lookups above answer "give me THIS asset, as THIS type", which is right for using one
         * and useless for listing them: a caller that wants to offer the user (or a control-channel
         * client) every material in the project has no handle to ask with and no business loading each
         * one to find out what it is.
         *
         * METADATA AND NOT ASSETS, for the reason FindMetadataByHandle gives next to it: there is nothing
         * here to cast, so the failure the typed lookups exist to prevent — a plausible pointer to an
         * object of another class — cannot occur. The record's own AssetType says what it is.
         *
         * The reference is into the manager's storage and is invalidated by anything that registers a new
         * asset. Callers walk it and copy what they keep; nothing here hands out a handle to hold.
         */
        [[nodiscard]] const AssetContainer& RegisteredAssets() const noexcept
        {
            return m_AssetsCache;
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