#include "FontCache.hpp"

#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/VFS.hpp>

#include <format>
#include <fstream>

namespace Desert::Text
{
    uint64_t FontCacheKey( const std::vector<uint8_t>& ttf, float pixelHeight,
                           const std::vector<uint32_t>& extraCodepoints )
    {
        constexpr uint64_t kFnvOffset = 1469598103934665603ull;
        constexpr uint64_t kFnvPrime  = 1099511628211ull;

        uint64_t h = kFnvOffset;
        h ^= kBakedFontCacheVersion;
        h *= kFnvPrime;
        h ^= static_cast<uint64_t>( static_cast<int>( pixelHeight ) );
        h *= kFnvPrime;
        for ( uint8_t b : ttf )
        {
            h ^= b;
            h *= kFnvPrime;
        }
        for ( uint32_t cp : extraCodepoints ) // a different glyph set is a different atlas
        {
            h ^= cp;
            h *= kFnvPrime;
        }
        return h;
    }

    std::filesystem::path FontCachePath( uint64_t key )
    {
        return Common::Constants::Path::COOKED_PATH / "FontCache" / std::format( "{:016x}.dfont", key );
    }

    BakedFont BakeFontForCache( const std::vector<uint8_t>& ttf, float pixelHeight,
                                const std::vector<uint32_t>& extraCodepoints )
    {
        // padding 5, atlas width 512 — the parameters kBakedFontCacheVersion stands for. Change them
        // by bumping the version, never here alone.
        return BakeFontSDF( ttf.data(), ttf.size(), pixelHeight, 5, 512, extraCodepoints );
    }

    bool TryLoadBakedFont( const std::filesystem::path& path, BakedFont& out )
    {
        // Loose file first — the dev override, and where this very run's bakes land.
        {
            std::error_code ec;
            const auto      size = std::filesystem::file_size( path, ec );
            if ( !ec && size > 0 )
            {
                std::ifstream in( path, std::ios::binary );
                if ( in )
                {
                    std::vector<uint8_t> bytes( static_cast<size_t>( size ) );
                    in.read( reinterpret_cast<char*>( bytes.data() ), static_cast<std::streamsize>( size ) );
                    if ( in )
                        return DeserializeBakedFont( bytes.data(), bytes.size(), out );
                }
            }
        }

        // Then the mounted archive — a packaged game's cooked atlases live ONLY here. Not routed
        // through FileSystem::ReadByteFileContent: that primitive logs an error for a missing file,
        // and a cache miss is the normal cold-start case, not an error.
        if ( auto packed = Common::Utils::VFS::ReadFile( path ) )
            return DeserializeBakedFont( reinterpret_cast<const uint8_t*>( packed->data() ), packed->size(), out );

        return false;
    }

    bool StoreBakedFont( const std::filesystem::path& path, const BakedFont& font )
    {
        std::error_code ec;
        std::filesystem::create_directories( path.parent_path(), ec );

        // Write-then-rename (И2) — see ShaderSpirvCache::StoreCachedSpirv: a half-written atlas under
        // a key that claims to be valid is worse than no atlas at all.
        const std::vector<uint8_t> bytes = SerializeBakedFont( font );
        return Common::Utils::FileSystem::WriteContentToFileAtomic(
             path, std::string( reinterpret_cast<const char*>( bytes.data() ), bytes.size() ) );
    }
} // namespace Desert::Text
