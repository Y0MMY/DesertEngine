#pragma once

#include <Common/Core/AssetHandle.hpp>

#include <string>

namespace Desert::Assets
{
    // DEPRECATED: the stable-id derivation now lives on the handle type itself —
    // use Common::AssetHandle::FromCookedPath() / FromKey(). This forwarder remains only so any
    // out-of-tree caller keeps compiling; it must NOT re-implement the hash (single source of truth).
    [[deprecated( "use Common::AssetHandle::FromKey / FromCookedPath" )]]
    inline Common::AssetHandle StableAssetId( const std::string& key )
    {
        return Common::AssetHandle::FromKey( key );
    }
} // namespace Desert::Assets
