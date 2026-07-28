#pragma once

#include <string>
#include <vector>

namespace Desert::Editor
{
    // A format-agnostic asset USAGE index. Assets in Desert point at each other by 64-bit handle
    // (materials embed texture handles, scenes/prefabs embed mesh/material handles, ...) and, in a few
    // formats, by path. Rather than teach this index every on-disk schema, it works by TOKENS: each
    // asset contributes the tokens a referencer would embed to point at it (its stable handle as
    // decimal text, its self-declared id, its project-relative path/name), and asset A references
    // asset B when A's raw file text contains any of B's tokens.
    //
    // This is a search aid, not a proof: numeric handle collisions are astronomically unlikely and
    // path strings are exact, but a bespoke binary format could hide a reference from a text scan. The
    // index therefore drives "find references" and "unused asset" discovery and a delete WARNING — it
    // never deletes on its own.
    //
    // The index itself is pure (std-only) so it is unit-tested directly; BuildProjectAssetReferenceIndex
    // (see AssetReferencesScan.cpp) is the engine-side adapter that fills it from the project on disk.
    class AssetReferenceIndex
    {
    public:
        struct Entry
        {
            std::string              Path;   // project-relative, '/'-separated identity of the asset
            std::string              Ext;    // lowercased extension incl. dot (".demat"); "" if none
            std::vector<std::string> Tokens; // strings a referencer embeds to point at THIS asset
            std::string              Text;   // raw contents, searched for OTHER entries' tokens ("" = binary)
        };

        void                      Clear();
        void                      Add( Entry entry );
        const std::vector<Entry>& Entries() const;

        // Paths of entries whose Text contains any Token of the entry at @p path (self excluded).
        std::vector<std::string> ReferencersOf( const std::string& path ) const;

        bool IsReferenced( const std::string& path ) const;

        // Entries with one of @p leafExts that nothing references — cleanup candidates. Roots (scenes,
        // prefabs) are naturally unreferenced, so callers pass only leaf extensions (textures, materials).
        std::vector<std::string> Orphans( const std::vector<std::string>& leafExts ) const;

    private:
        const Entry* Find( const std::string& path ) const;

        std::vector<Entry> m_Entries;
    };

    // Scans the currently-open project's Assets tree and fills @p index (clears it first). No-op
    // without an open project. Text formats are read for scanning; binaries contribute tokens only.
    void BuildProjectAssetReferenceIndex( AssetReferenceIndex& index );
} // namespace Desert::Editor
