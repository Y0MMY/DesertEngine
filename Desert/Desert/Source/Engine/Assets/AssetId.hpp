#pragma once

#include <Common/Core/UUID.hpp>

#include <cstdint>
#include <string>

namespace Desert::Assets
{
    // Deterministic asset handle derived from a stable key (FNV-1a 64). The intended key is the asset's
    // COOKED path (relative, forward-slash, normalized) so a handle can be computed from a path WITHOUT
    // reading the payload — this is what lets the asset registry scan cooked dirs cheaply and lets a handle
    // survive re-cooks. Same key -> same handle. (Mirrors the per-importer StableTexture/MaterialId helpers;
    // those can migrate onto this.)
    inline Common::UUID StableAssetId( const std::string& key )
    {
        uint64_t h = 1469598103934665603ull;
        for ( unsigned char c : key )
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        return Common::UUID( h ? h : 1 ); // never collide with the null handle
    }
} // namespace Desert::Assets
