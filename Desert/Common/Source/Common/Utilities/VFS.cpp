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

        // One absolute, symlink-resolved spelling for an incoming path. weakly_canonical, not
        // lexically_normal alone, because two spellings of ONE directory must compare equal: on macOS
        // the temp tree is reached both as /var/... (a symlink) and /private/var/... (what getcwd
        // returns), and comparing an as-spelled mount root against a resolved current_path made every
        // relative lookup under a symlinked prefix miss the pak. The tail may not exist anywhere but
        // the archive — weakly_canonical resolves the existing prefix and keeps the rest lexical.
        std::filesystem::path CanonicalAbs( const std::filesystem::path& path )
        {
            std::error_code             ec;
            const std::filesystem::path raw =
                 path.is_absolute() ? path.lexically_normal()
                                    : ( std::filesystem::current_path( ec ) / path ).lexically_normal();

            std::error_code             canonEc;
            const std::filesystem::path canon = std::filesystem::weakly_canonical( raw, canonEc );
            return ( canonEc || canon.empty() ) ? raw : canon;
        }

        // Archive key for an incoming (pre-canonicalized) path within ONE mount: relative to that
        // mount's root. nullopt when the path points outside the mounted tree.
        std::optional<std::string> KeyFor( const Mount& mount, const std::filesystem::path& abs )
        {
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
            if ( s_Mounts.empty() ) // dev: nothing mounted, skip the canonicalization syscalls
                return {};
            const std::filesystem::path abs = CanonicalAbs( path ); // once, not per mount
            for ( auto it = s_Mounts.rbegin(); it != s_Mounts.rend(); ++it )
            {
                const auto key = KeyFor( *it, abs );
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

        Mount mount;
        // The root must live in the same canonical spelling KeyFor produces for lookups, or a pak
        // mounted through a symlink (macOS /var -> /private/var) can never resolve anything.
        mount.Root = CanonicalAbs( pakFile ).parent_path();
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
        if ( s_Mounts.empty() ) // dev: nothing mounted, skip the canonicalization syscalls entirely
            return false;
        const std::filesystem::path abs = CanonicalAbs( path );
        for ( auto it = s_Mounts.rbegin(); it != s_Mounts.rend(); ++it )
            if ( const auto key = KeyFor( *it, abs ); key && it->Pak->Contains( *key ) )
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

        if ( s_Mounts.empty() )
            return result;
        const std::filesystem::path abs = CanonicalAbs( directory );
        for ( auto it = s_Mounts.rbegin(); it != s_Mounts.rend(); ++it )
        {
            auto prefix = KeyFor( *it, abs );
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
