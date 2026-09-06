#pragma once

#include <Common/Core/AssetHandle.hpp>

#include <cctype>
#include <cstdint>
#include <string>

namespace Desert::Editor::ThumbnailKey
{
    /**
     * @brief Which cached thumbnail belongs to which asset.
     *
     * WHY THE ANSWER IS NOT "THE PATH THE CALLER HAPPENED TO HOLD". The key used to be the caller's own
     * spelling with every non-alphanumeric byte turned into '_', which made it a property of the MACHINE
     * rather than of the asset: the file this sentence was written next to was really called
     * `_Users_daniilsavcenko_Desktop_..._Editor_Resources_Assets_Materials_Starter_Prop_demat.png`.
     * Measured consequence, in the editor, in one `Cooked/Thumbnails` directory: opening one project
     * through two equivalent spellings of its own path (`<proj>/Editor` and a symlink to it) captured the
     * SAME three materials twice and left SIX files behind — three pictures nobody can invalidate,
     * because the panel that would invalidate them only knows one of the two names. Renaming the project
     * folder does the same thing to every thumbnail at once.
     *
     * So the key is the asset's identity, and this engine already has exactly one answer to "which asset
     * is this, whatever the spelling": `Common::AssetHandle::StableKeyForPath`, the project-relative,
     * root-tagged key the AssetManager deduplicates its registry on. Deriving the thumbnail key from the
     * same function is the point — a thumbnail is a picture OF an asset, so the two must not be able to
     * disagree about what an asset is. That is also why nothing here folds case: the identity key does
     * not, and a thumbnail key that case-folded while the asset handle did not would be a second, subtly
     * different notion of sameness — the exact two-keys-that-must-agree shape this replaces.
     *
     * A path under no content root keeps its normalized absolute spelling, because that is what
     * StableKeyForPath returns for a file genuinely outside the project: such a file has no
     * project-relative identity to give it, and inventing one here would make two projects' strays
     * collide.
     */
    inline std::string Identity( const std::string& assetPath )
    {
        return Common::AssetHandle::StableKeyForPath( assetPath );
    }

    /**
     * @brief The cache file name for an asset: a readable flattening of its identity, then the identity's
     *        own hash.
     *
     * The hash is not decoration. Flattening `assets:Materials/Wood/Oak.demat` for the filesystem loses
     * the difference between '/' and '_', so `Materials/Wood_Oak.demat` and `Materials/Wood/Oak.demat`
     * flatten to ONE name — two different assets sharing a cache entry, i.e. one of them showing the
     * other's picture with nothing able to tell them apart. Appending
     * `Common::AssetHandle::FromKey(identity)` restores injectivity, and it is not a new number: it is
     * precisely the path-derived handle the asset itself carries (`AssetHandle::FromCookedPath`), so the
     * file is named after the asset's own id rather than after a second hash invented here.
     *
     * The readable half stays because it is what makes the cache inspectable from a shell — that is how
     * the absolute-path defect above was found.
     */
    inline std::string FileName( const std::string& assetPath )
    {
        const std::string identity = Identity( assetPath );

        std::string readable = identity;
        for ( char& c : readable )
        {
            if ( !std::isalnum( static_cast<unsigned char>( c ) ) )
                c = '_';
        }

        const std::uint64_t id = static_cast<std::uint64_t>( Common::AssetHandle::FromKey( identity ) );
        return readable + '_' + std::to_string( id ) + ".png";
    }
} // namespace Desert::Editor::ThumbnailKey
