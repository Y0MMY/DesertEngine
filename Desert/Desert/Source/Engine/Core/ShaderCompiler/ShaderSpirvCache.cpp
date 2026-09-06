#include "ShaderSpirvCache.hpp"

#include <Common/Core/Constants.hpp>
#include <Common/Utilities/VFS.hpp>

#include <cstring>
#include <format>
#include <fstream>

namespace Desert::Core
{
    std::filesystem::path SpirvCachePathForKey( uint64_t key )
    {
        return Common::Constants::Path::COOKED_PATH / "ShaderCache" / std::format( "{:016x}.spv", key );
    }

    std::optional<std::vector<uint32_t>> TryLoadCachedSpirv( uint64_t key )
    {
        const auto path = SpirvCachePathForKey( key );

        // A whole number of 32-bit words or it is not SPIR-V. Checked for both halves below.
        const auto toWords = []( const char* bytes, size_t size ) -> std::optional<std::vector<uint32_t>>
        {
            if ( size == 0 || ( size % sizeof( uint32_t ) ) != 0 )
                return std::nullopt;
            std::vector<uint32_t> words( size / sizeof( uint32_t ) );
            std::memcpy( words.data(), bytes, size );
            return words;
        };

        // Loose file first — the dev override, and where this very run's compiles land.
        {
            std::error_code ec;
            const auto      size = std::filesystem::file_size( path, ec );
            if ( !ec && size > 0 )
            {
                std::ifstream in( path, std::ios::binary );
                if ( in )
                {
                    std::vector<char> bytes( static_cast<size_t>( size ) );
                    in.read( bytes.data(), static_cast<std::streamsize>( size ) );
                    if ( in )
                        return toWords( bytes.data(), bytes.size() );
                }
            }
        }

        // Then the mounted archive — a packaged game's cooked artifacts live ONLY here. Not routed
        // through FileSystem::ReadByteFileContent on purpose: that primitive logs an error for a
        // missing file, and a cache miss is not an error, it is the normal cold-start case.
        if ( auto packed = Common::Utils::VFS::ReadFile( path ) )
            return toWords( packed->data(), packed->size() );

        return std::nullopt;
    }

    void StoreCachedSpirv( uint64_t key, const std::vector<uint32_t>& spirv )
    {
        const auto      path = SpirvCachePathForKey( key );
        std::error_code ec;
        std::filesystem::create_directories( path.parent_path(), ec );
        std::ofstream out( path, std::ios::binary | std::ios::trunc );
        if ( !out ) // read-only install (e.g. inside an .app bundle) — cache is best-effort
            return;
        out.write( reinterpret_cast<const char*>( spirv.data() ),
                   static_cast<std::streamsize>( spirv.size() * sizeof( uint32_t ) ) );
    }
} // namespace Desert::Core
