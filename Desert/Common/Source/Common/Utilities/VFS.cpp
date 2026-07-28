#include "VFS.hpp"

#include "PakFile.hpp"

#include <Common/Core/Logger.hpp>

#include <memory>

namespace Common::Utils
{
    namespace
    {
        std::unique_ptr<PakReader> s_Pak;
        std::filesystem::path      s_MountRoot; // absolute, normalized

        // Archive key for an incoming path: absolute-normalized, then relative to the mount root.
        // nullopt when the path points outside the mounted tree.
        std::optional<std::string> KeyFor( const std::filesystem::path& path )
        {
            if ( !s_Pak )
                return std::nullopt;

            std::error_code ec;
            std::filesystem::path abs =
                 path.is_absolute() ? path.lexically_normal()
                                    : ( std::filesystem::current_path( ec ) / path ).lexically_normal();

            const std::filesystem::path rel = abs.lexically_relative( s_MountRoot );
            if ( rel.empty() || rel.begin()->string() == ".." )
                return std::nullopt;
            return rel.generic_string();
        }
    } // namespace

    bool VFS::MountPak( const std::filesystem::path& pakFile )
    {
        auto reader = std::make_unique<PakReader>( pakFile );
        if ( !reader->IsOpen() )
        {
            LOG_WARN( "[VFS] Could not mount {} (missing or corrupt)", pakFile.string() );
            return false;
        }

        std::error_code ec;
        s_MountRoot = std::filesystem::absolute( pakFile, ec ).parent_path().lexically_normal();
        s_Pak       = std::move( reader );
        LOG_INFO( "[VFS] Mounted {} ({} entries, root {})", pakFile.string(), s_Pak->EntryCount(),
                  s_MountRoot.string() );
        return true;
    }

    bool VFS::IsMounted()
    {
        return static_cast<bool>( s_Pak );
    }

    void VFS::Unmount()
    {
        s_Pak.reset();
        s_MountRoot.clear();
    }

    bool VFS::Exists( const std::filesystem::path& path )
    {
        const auto key = KeyFor( path );
        return key && s_Pak->Contains( *key );
    }

    std::optional<std::string> VFS::ReadFile( const std::filesystem::path& path )
    {
        const auto key = KeyFor( path );
        if ( !key )
            return std::nullopt;
        return s_Pak->Read( *key );
    }

    std::optional<uint64_t> VFS::FileSize( const std::filesystem::path& path )
    {
        const auto key = KeyFor( path );
        if ( !key )
            return std::nullopt;
        return s_Pak->EntrySize( *key );
    }

    std::vector<std::filesystem::path> VFS::ListFiles( const std::filesystem::path& directory )
    {
        std::vector<std::filesystem::path> result;
        if ( !s_Pak )
            return result;

        auto prefix = KeyFor( directory );
        if ( !prefix )
            return result;
        std::string p = *prefix;
        if ( p == "." )
            p.clear(); // the mount root itself
        else if ( !p.empty() && p.back() != '/' )
            p += '/';

        for ( const auto& key : s_Pak->KeysWithPrefix( p ) )
            result.push_back( s_MountRoot / std::filesystem::path( key ) );
        return result;
    }
} // namespace Common::Utils
