#include "LODFold.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace Desert::Editor
{
    namespace
    {
        namespace Ser = Assets::Serialization;

        // "<base>_LOD<n>" (case-insensitive suffix) -> { base, level }. Level 0 = the base mesh (no suffix or
        // _LOD0). A non-numeric / absent suffix is treated as level 0 (a plain mesh).
        std::pair<std::string, int> ParseLOD( const std::string& name )
        {
            std::string lower = name;
            std::transform( lower.begin(), lower.end(), lower.begin(),
                            []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

            const auto pos = lower.rfind( "_lod" );
            if ( pos == std::string::npos )
                return { name, 0 };

            const std::string digits = name.substr( pos + 4 );
            if ( digits.empty() || !std::all_of( digits.begin(), digits.end(),
                                                 []( unsigned char c ) { return std::isdigit( c ) != 0; } ) )
                return { name, 0 };

            return { name.substr( 0, pos ), std::stoi( digits ) };
        }
    } // namespace

    int FoldExternalLODMeshes( Ser::MeshAssetData& data )
    {
        if ( data.IsSkinned || data.Submeshes.empty() )
            return 0;

        // Group submesh indices by base name; within a group, level 0 is the base and 1.. are LOD siblings.
        struct Group
        {
            int              Base = -1; // submesh index of level 0
            std::vector<int> Lods;      // submesh indices for levels 1..N (in ascending level order)
        };
        std::unordered_map<std::string, Group>                            groups;
        std::unordered_map<std::string, std::vector<std::pair<int, int>>> byBase; // base -> {(level, submeshIdx)}
        for ( int i = 0; i < static_cast<int>( data.Submeshes.size() ); ++i )
        {
            const auto [base, level] = ParseLOD( data.Submeshes[i].Name );
            byBase[base].push_back( { level, i } );
        }

        bool anyLods = false;
        for ( auto& [base, entries] : byBase )
        {
            std::sort( entries.begin(), entries.end() );
            Group g;
            for ( const auto& [level, idx] : entries )
            {
                if ( level == 0 && g.Base < 0 )
                    g.Base = idx;
                else if ( level > 0 )
                {
                    g.Lods.push_back( idx );
                    anyLods = true;
                }
            }
            if ( g.Base < 0 && !g.Lods.empty() )
            {
                // No explicit level-0: promote the lowest-level sibling to base.
                g.Base = g.Lods.front();
                g.Lods.erase( g.Lods.begin() );
            }
            groups[base] = std::move( g );
        }
        if ( !anyLods )
            return 0;

        // Rebuild vertices / indices / submeshes, folding LOD siblings into their base and dropping them.
        std::vector<Ser::StaticVertexData> newVerts;
        std::vector<Ser::IndexData>        newIndices;
        std::vector<Ser::SubmeshData>      newSubs;
        newVerts.reserve( data.StaticVertices.size() );
        newIndices.reserve( data.Indices.size() );

        std::vector<bool> consumed( data.Submeshes.size(), false );
        int               foldedLevels = 0;

        // Copies a source submesh's vertices + its base indices into the new buffers, returns the new submesh.
        const auto emitBase = [&]( int si )
        {
            const Ser::SubmeshData& src = data.Submeshes[si];
            Ser::SubmeshData        out = src;
            out.VertexOffset            = static_cast<uint32_t>( newVerts.size() );
            out.IndexOffset             = static_cast<uint32_t>( newIndices.size() * 3 );
            out.LODs.clear();

            for ( uint32_t v = 0; v < src.VertexCount; ++v )
                newVerts.push_back( data.StaticVertices[src.VertexOffset + v] );
            const uint32_t triStart = src.IndexOffset / 3;
            for ( uint32_t t = 0; t < src.IndexCount / 3; ++t )
                newIndices.push_back( data.Indices[triStart + t] );
            out.VertexCount = src.VertexCount;
            out.IndexCount  = src.IndexCount;
            return out;
        };

        for ( int i = 0; i < static_cast<int>( data.Submeshes.size() ); ++i )
        {
            if ( consumed[i] )
                continue;

            const auto [base, level] = ParseLOD( data.Submeshes[i].Name );
            const Group& g           = groups[base];

            // A LOD sibling that isn't a base is emitted by its base's pass -> skip standalone.
            if ( g.Base != i && level > 0 )
                continue;

            Ser::SubmeshData out = emitBase( g.Base );
            consumed[g.Base]     = true;
            out.Name             = base; // strip the _LODn suffix on the merged submesh

            // Append each LOD sibling's vertices INTO this submesh's block; its indices (offset to the appended
            // verts, still submesh-local) become one LOD level.
            for ( int lodIdx : g.Lods )
            {
                consumed[lodIdx]            = true;
                const Ser::SubmeshData& lod = data.Submeshes[lodIdx];
                if ( lod.VertexCount == 0 || lod.IndexCount < 3 )
                    continue;

                const uint32_t vBase = out.VertexCount; // offset of the LOD verts within this submesh
                for ( uint32_t v = 0; v < lod.VertexCount; ++v )
                    newVerts.push_back( data.StaticVertices[lod.VertexOffset + v] );
                out.VertexCount += lod.VertexCount;

                std::vector<Ser::IndexData> tris;
                tris.reserve( lod.IndexCount / 3 );
                const uint32_t lodTriStart = lod.IndexOffset / 3;
                for ( uint32_t t = 0; t < lod.IndexCount / 3; ++t )
                {
                    const auto& f = data.Indices[lodTriStart + t];
                    tris.push_back( { f.V1 + vBase, f.V2 + vBase, f.V3 + vBase } );
                }
                out.LODs.push_back( std::move( tris ) );
                ++foldedLevels;
            }

            newSubs.push_back( std::move( out ) );
        }

        data.StaticVertices = std::move( newVerts );
        data.Indices        = std::move( newIndices );
        data.Submeshes      = std::move( newSubs );
        return foldedLevels;
    }
} // namespace Desert::Editor
