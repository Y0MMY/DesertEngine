#pragma once

#include "Common.hpp"

namespace Desert::Assets
{
    struct AssetMetadata
    {
        // Every field has a default, and the defaults spell "no asset". IsValid() below tests the handle
        // against 0; leaving Handle without an initializer made a default-constructed metadata report
        // itself VALID back when UUID's default was random, and left Priority and AssetType reading
        // uninitialized memory.
        Common::UUID     Handle = Common::UUID::Null();
        Common::Filepath Filepath;
        AssetPriority    Priority  = AssetPriority::Low;
        AssetTypeID      AssetType = AssetTypeID::Unknown;
        /*std::unordered_map<std::string, std::variant<int, float, std::string>> AdditionalData;

        template <typename T>
        void SetAdditionalData( const std::string& key, const T& value )
        {
            AdditionalData[key] = value;
        }

        template <typename T>
        T GetAdditionalData( const std::string& key, const T& defaultValue = T{} ) const
        {

        }*/

        bool IsValid() const
        {
            return Handle != 0 && ( !Filepath.empty() ) && AssetType != AssetTypeID::Unknown;
        }

        // The asset's identity as a string: exactly the key its handle is hashed from. The registry
        // deduplicates on this, so "which file is this?" is answered ONCE, in one place, for both.
        //
        // This replaces IsEquivalent(), which compared raw path strings. That agreed with identity only
        // while identity was also spelling-sensitive; once a handle became a function of the asset's
        // place in the project, the two keys disagreed, and a second spelling of an already-registered
        // file produced a second record that then took over the handle in the lookup. Two keys that must
        // agree and nothing checking that they do is the defect class this programme keeps paying for,
        // so there is now one key.
        //
        // It is the KEY and not the handle, because Texture2D and Material overwrite their handle from
        // an id stored in their file during Load: a not-yet-loaded lookup record carries the
        // path-derived value and would never match a loaded one.
        //
        // Computed on demand rather than cached in a member, so it cannot fall out of step with
        // Filepath. That is affordable only because the registry asks once per asset; asking once per
        // COMPARISON cost 56.9 s on a 2000-asset preload against 207 ms before.
        std::string StableKey() const
        {
            return Common::AssetHandle::StableKeyForPath( Filepath );
        }
    };
} // namespace Desert::Assets