#pragma once

#include <Common/Core/Constants.hpp>
#include <Common/Core/UUID.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Common
{
    // A first-class asset handle. Derives from UUID so it stays a standard-layout 8-byte value that is
    // implicitly interconvertible with the raw uint64 (serialization, service maps, comparisons all keep
    // working unchanged), while OWNING the stable-id derivation that used to live in the free
    // Desert::Assets::StableAssetId() helper.
    //
    // What it adds over UUID:
    //   * FromCookedPath()/FromKey() give deterministic, path-derived handles that survive re-cooks and
    //     restarts and are computable WITHOUT reading the asset payload.
    // (The null default ctor is no longer a difference: UUID's default is null too, for the reasons in
    // UUID.hpp. Both are spelled out here anyway because this is the type asset code reads.)
    //
    // The 64-bit value and its serialized form are still a plain uint64. The VALUE a given path derives
    // changed when FromCookedPath became project-relative; nothing in the repository referenced a
    // path-derived handle by number, which is why that re-stamp needed no migration — the measurement
    // behind that claim is in the AssetHandleStability suite.
    class AssetHandle : public UUID
    {
    public:
        AssetHandle() noexcept : UUID( static_cast<uint64_t>( 0 ) ) // Null / unset — NOT random
        {
        }

        AssetHandle( uint64_t value ) noexcept : UUID( value )
        {
        }

        AssetHandle( const UUID& uuid ) noexcept : UUID( uuid )
        {
        }

        // Deterministic FNV-1a (64) over an arbitrary stable key. Same key -> same handle.
        static AssetHandle FromKey( std::string_view key ) noexcept
        {
            uint64_t h = 1469598103934665603ull;
            for ( unsigned char c : key )
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            return AssetHandle( h ? h : 1ull ); // never collide with the null handle
        }

        // The roots an asset can live under, each with the TAG its relative paths are hashed behind.
        //
        // Why a TAG and not the bare relative path: `Cooked/Textures/T.tex` and
        // `Resources/Assets/Textures/T.tex` both reduce to `Textures/T.tex`, so without a tag a cooked
        // asset and a content asset that happen to sit at mirrored offsets would share one handle. The
        // tag is part of the hashed key, never of any path on disk.
        //
        // Why THESE three: they are exactly the roots AssetPreloader scans. Content and cooked assets
        // move with the project (Constants::Path::SetProjectRoot rewrites them); engine resources never
        // do. Longest match wins, which is what makes the default sandbox layout — where ASSETS_PATH
        // (`Resources/Assets/`) is NESTED INSIDE RESOURCE_PATH (`Resources/`) — resolve to `assets`
        // rather than to `engine`, without the answer depending on the order of this array.
        struct PathRoot
        {
            const std::filesystem::path* Root;
            std::string_view             Tag;
        };

        // Builds the stable key a path-derived handle is hashed from: the path RELATIVE to whichever
        // root contains it, behind that root's tag.
        //
        // WHY RELATIVE. This used to hash whatever spelling the caller happened to hold. Two measured
        // consequences: `Assets/Clouds/X.dcnv` hashed to 122788169303960361 while the same file spelled
        // absolutely hashed to 868888776058461864 — one file, two identities — and because every content
        // root turns absolute the moment a `.deproj` is opened (Constants::Path::SetProjectRoot), the
        // absolute form is the one that won in practice. That form encodes the developer's home
        // directory, so a handle written down on one machine named nothing on any other. The three cloud
        // branches in ComponentRegistry::MakeAssetResolver already store their paths relative to
        // ASSETS_PATH for exactly this reason, and the reasoning is written out there; this is that same
        // rule applied where identity is actually minted, so it holds for every asset class instead of
        // the three somebody remembered.
        //
        // A path under NO root keeps its normalized spelling, unchanged from before. That is deliberate
        // on two counts: a file genuinely outside the project has no project-relative identity to give
        // it (ComponentRegistry says the same thing in the same situation — "outside the project, say so
        // plainly"), and the synthetic keys that are not filesystem paths at all — `procedural://` clips,
        // `memory://` sequencer clips — must keep hashing to what they always did rather than acquire a
        // dependency on the process's working directory.
        static std::string StableKeyForPath( const std::filesystem::path& path ) noexcept
        {
            namespace fs = std::filesystem;

            const fs::path normalized = path.lexically_normal();

            // Absolute forms are used ONLY to decide which root contains the path. Comparing the two
            // spellings directly cannot work: with a project open the roots are absolute while callers
            // still pass working-directory-relative strings (shaders always do — SHADERDIR_PATH is const
            // and is never remapped), and with no project open it is the other way round.
            std::error_code ec;
            const fs::path  absolutePath = fs::absolute( normalized, ec ).lexically_normal();
            if ( ec )
                return normalized.generic_string();

            const PathRoot roots[] = {
                 { &Constants::Path::ASSETS_PATH, "assets" },
                 { &Constants::Path::COOKED_PATH, "cooked" },
                 { &Constants::Path::RESOURCE_PATH, "engine" },
            };

            std::string_view bestTag;
            std::string      bestRelative;
            std::size_t      bestRootLength = 0;

            for ( const PathRoot& candidate : roots )
            {
                std::error_code rootEc;
                const fs::path  absoluteRoot = fs::absolute( *candidate.Root, rootEc ).lexically_normal();
                if ( rootEc )
                    continue;

                const std::string relative = absolutePath.lexically_relative( absoluteRoot ).generic_string();

                // Empty means the two paths have no relation at all; "." means the path IS the root; a
                // leading ".." means the path escapes upwards, i.e. it is not under this root.
                if ( relative.empty() || relative == "." || relative.rfind( "..", 0 ) == 0 )
                    continue;

                const std::size_t rootLength = absoluteRoot.generic_string().size();
                if ( rootLength > bestRootLength )
                {
                    bestRootLength = rootLength;
                    bestTag        = candidate.Tag;
                    bestRelative   = relative;
                }
            }

            if ( bestRootLength == 0 )
                return normalized.generic_string();

            return std::string( bestTag ) + ':' + bestRelative;
        }

        // Deterministic handle from an asset's path, keyed on its location RELATIVE to the project (see
        // StableKeyForPath) so the same file carries the same handle on every machine and under every
        // spelling. Computable without parsing the (large) payload.
        static AssetHandle FromCookedPath( const std::filesystem::path& cookedPath ) noexcept
        {
            return FromKey( StableKeyForPath( cookedPath ) );
        }

        // Fresh, random runtime id — for assets with no stable source path (procedural / builtin meshes).
        static AssetHandle Generate() noexcept
        {
            return AssetHandle( UUID::Generate() );
        }

        static AssetHandle Null() noexcept
        {
            return AssetHandle();
        }
    };
} // namespace Common

namespace std
{
    template <>
    struct hash<Common::AssetHandle>
    {
        std::size_t operator()( const Common::AssetHandle& handle ) const noexcept
        {
            return static_cast<std::size_t>( static_cast<uint64_t>( handle ) );
        }
    };
} // namespace std
