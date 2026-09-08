#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Desert::Editor
{
    /**
     * @brief THE PROJECT'S OPENABLE FILES, TURNED INTO PALETTE ENTRIES. Pure: files in, labels out.
     *
     * ── WHY THIS EXISTS AT ALL, AND WHAT IT REPLACES ─────────────────────────────────────────────────
     *
     * The palette's `Open` group used to be built by walking the ASSET MANAGER'S CACHE — the records the
     * startup preloader had got round to registering. That is a container whose contents are derived from
     * the same source as the question being asked of it, which is the shape §1.4 of the contract tells you
     * to watch for, and it failed in the way that shape always fails.
     *
     * Measured on this repository's own project, polling once per frame through the control channel: the
     * group goes 0 -> 106 -> 130 entries. FIVE separate startup stages fill that cache (meshes and
     * materials, noise volumes, cloud types, hero clouds, painted layouts), so between the third and the
     * eighth the palette successfully offers every material in the project and NOT ONE of its twenty-four
     * cloud assets — for 3.3 seconds of every single boot, on an idle machine. A client that asked once
     * and believed the answer concluded the project has no cloud documents. There is nothing in the reply
     * that distinguishes that from the truth.
     *
     * ── THE RULE ─────────────────────────────────────────────────────────────────────────────────────
     *
     * ENUMERATE FROM WHAT DESCRIBES EXISTENCE, NOT FROM WHAT HAPPENS TO BE LOADED. The entity half of the
     * palette has always done this — it walks `Scene::GetAllEntities()` against the registered subject
     * types, and there is no race in it BY CONSTRUCTION, because the scene is what says which entities
     * exist. For files, the thing that says which files exist is the content enumeration
     * (`Common::Utils::FileSystem::ListFilesRecursive`), which is also the enumeration the preloader
     * itself walks to BUILD the cache. Reading the same list one step earlier removes the window instead
     * of shortening it.
     *
     * ── AND HOW ONE IS ADDRESSED ─────────────────────────────────────────────────────────────────────
     *
     * BY ITS PATH UNDER THE CONTENT ROOT, never by `filename()`. This project has three files called
     * `model.demat` today — `Materials/base/model.demat`, `Materials/base_basic_pbr/model.demat` and
     * `Materials/base_basic_shaded/model.demat` — so the old labels produced three palette rows spelled
     * identically, and `run Open model.demat` matched all three: ResolveCommand keeps scanning after a hit
     * (the candidate count is part of its answer), so the LAST one silently won. Three indistinguishable
     * rows, one of which is what you get.
     *
     * The same lesson was already learnt one group over and written down there: SceneLabel names a level
     * by its path relative to the scenes root, "not the bare filename... two Test.desce in different
     * folders read identically". The `Open` group simply never got the fix.
     *
     * A path is chosen over a GUID because both are unambiguous and only one can be typed by the person
     * this list also serves: the palette is Ctrl+P, and "Materials/base/model.demat" is searchable in a
     * way that a sixty-four-bit handle is not. The label is generic (forward slashes) so the string a
     * client sends is the same on every platform, and it is exactly the key form a content manifest uses.
     *
     * Nothing here opens anything, knows what a material is, or touches a filesystem: it takes the files
     * it is given. That is what makes the addressing rule assertable by a suite, which it has to be —
     * EditorLayer.cpp, where the palette is assembled, is compiled by no suite at all
     * (scripts/CI/UnreachedSources.sh).
     */
    struct OpenableAsset
    {
        /// The palette label AND the address. Content-root-relative, generic separators.
        std::string Label;
        /// What the path openers are handed. The full path, because that is what resolves a file.
        std::string Path;
    };

    /// Lower-cased extension, dot included, of @p path. Free because the filter and the tests both want it
    /// and a second spelling of "compare extensions case-insensitively" is a second answer.
    [[nodiscard]] inline std::string LowercaseExtension( const std::filesystem::path& path )
    {
        std::string extension = path.extension().string();
        std::transform( extension.begin(), extension.end(), extension.begin(),
                        []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
        return extension;
    }

    /**
     * @brief The files in @p files that some registered editor claims, labelled unambiguously.
     *
     * @param files       every file the content enumeration found — disk and mounted pak alike.
     * @param claimed     the extensions the registered path openers answer for, dot included, lower case.
     *                    DERIVED FROM THE REGISTRATIONS and never typed here: a document type that
     *                    registers an opener appears in this list with nobody remembering to add it, which
     *                    is the census this whole file exists so that nobody has to refill.
     * @param contentRoot what labels are relative to.
     *
     * Sorted by label, so the palette's order is a property of the project rather than of the order the
     * filesystem happened to walk it in — two runs of the same client must offer the same list.
     *
     * A file OUTSIDE @p contentRoot keeps its whole generic path rather than a "../../.." chain, which
     * names nothing a person could act on. It stays unambiguous either way, which is the property that
     * matters: two distinct files cannot produce one label.
     */
    [[nodiscard]] inline std::vector<OpenableAsset>
    CollectOpenableAssets( const std::vector<std::filesystem::path>& files,
                           const std::vector<std::string>& claimed, const std::filesystem::path& contentRoot )
    {
        std::vector<OpenableAsset> openable;

        for ( const std::filesystem::path& file : files )
        {
            const std::string extension = LowercaseExtension( file );
            if ( std::find( claimed.begin(), claimed.end(), extension ) == claimed.end() )
                continue;

            std::error_code   ec;
            const std::string relative = std::filesystem::relative( file, contentRoot, ec ).generic_string();

            const bool outside = ec || relative.empty() || relative.rfind( "..", 0 ) == 0;
            openable.push_back(
                 OpenableAsset{ outside ? file.generic_string() : relative, file.generic_string() } );
        }

        std::sort( openable.begin(), openable.end(),
                   []( const OpenableAsset& a, const OpenableAsset& b ) { return a.Label < b.Label; } );
        return openable;
    }
} // namespace Desert::Editor
