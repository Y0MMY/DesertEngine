#pragma once

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
    // The 64-bit value and its serialized form are unchanged, so existing cooked assets / saved scenes
    // resolve identically.
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

        // Deterministic handle from an asset's COOKED path (normalized, forward-slash) so the registry can
        // compute the same handle from a path without parsing the (large) payload.
        static AssetHandle FromCookedPath( const std::filesystem::path& cookedPath ) noexcept
        {
            return FromKey( cookedPath.lexically_normal().generic_string() );
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
