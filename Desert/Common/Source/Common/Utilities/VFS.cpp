#include "VFS.hpp"

#include "PakFile.hpp"

#include <Common/Core/Logger.hpp>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

namespace Common::Utils
{
    namespace
    {
        struct Mount
        {
            std::unique_ptr<PakReader> Pak;
            std::filesystem::path      Root; // absolute, normalized
        };

        // Mount STACK: later mounts override earlier ones for the same key (base.dpak first, then
        // per-type chunks, then patch paks — the patch wins). Lookups walk it newest-first.
        std::vector<Mount> s_Mounts;

        // Archive key for an incoming path within ONE mount: absolute-normalized, then relative to
        // that mount's root. nullopt when the path points outside the mounted tree.
        std::optional<std::string> KeyFor( const Mount& mount, const std::filesystem::path& path )
        {
            std::error_code ec;
            std::filesystem::path abs =
                 path.is_absolute() ? path.lexically_normal()
                                    : ( std::filesystem::current_path( ec ) / path ).lexically_normal();

            const std::filesystem::path rel = abs.lexically_relative( mount.Root );
            if ( rel.empty() || rel.begin()->string() == ".." )
                return std::nullopt;
            return rel.generic_string();
        }

        // Newest-first resolution of a path to the mount that owns it.
        template <typename Fn>
        auto Resolve( const std::filesystem::path& path, Fn&& fn )
             -> decltype( fn( *s_Mounts.front().Pak, std::string{} ) )
        {
            for ( auto it = s_Mounts.rbegin(); it != s_Mounts.rend(); ++it )
            {
                const auto key = KeyFor( *it, path );
                if ( !key || !it->Pak->Contains( *key ) )
                    continue;
                return fn( *it->Pak, *key );
            }
            return {};
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
        Mount           mount;
        mount.Root = std::filesystem::absolute( pakFile, ec ).parent_path().lexically_normal();
        mount.Pak  = std::move( reader );
        LOG_INFO( "[VFS] Mounted {} ({} entries, root {}, priority {})", pakFile.string(),
                  mount.Pak->EntryCount(), mount.Root.string(), s_Mounts.size() );
        s_Mounts.push_back( std::move( mount ) );
        return true;
    }

    bool VFS::IsMounted()
    {
        return !s_Mounts.empty();
    }

    void VFS::Unmount()
    {
        s_Mounts.clear();
    }

    bool VFS::Exists( const std::filesystem::path& path )
    {
        for ( auto it = s_Mounts.rbegin(); it != s_Mounts.rend(); ++it )
            if ( const auto key = KeyFor( *it, path ); key && it->Pak->Contains( *key ) )
                return true;
        return false;
    }

    std::optional<std::string> VFS::ReadFile( const std::filesystem::path& path )
    {
        return Resolve( path, []( const PakReader& pak, const std::string& key ) { return pak.Read( key ); } );
    }

    std::optional<uint64_t> VFS::FileSize( const std::filesystem::path& path )
    {
        return Resolve( path,
                        []( const PakReader& pak, const std::string& key ) { return pak.EntrySize( key ); } );
    }

    std::vector<std::filesystem::path> VFS::ListFiles( const std::filesystem::path& directory )
    {
        // Union across the stack, newest mount first, deduped by full path — a patched file lists once.
        std::vector<std::filesystem::path>  result;
        std::unordered_set<std::string>     seen;

        for ( auto it = s_Mounts.rbegin(); it != s_Mounts.rend(); ++it )
        {
            auto prefix = KeyFor( *it, directory );
            if ( !prefix )
                continue;
            std::string p = *prefix;
            if ( p == "." )
                p.clear(); // the mount root itself
            else if ( !p.empty() && p.back() != '/' )
                p += '/';

            for ( const auto& key : it->Pak->KeysWithPrefix( p ) )
            {
                const std::filesystem::path full = it->Root / std::filesystem::path( key );
                if ( seen.insert( full.generic_string() ).second )
                    result.push_back( full );
            }
        }
        return result;
    }
} // namespace Common::Utils
