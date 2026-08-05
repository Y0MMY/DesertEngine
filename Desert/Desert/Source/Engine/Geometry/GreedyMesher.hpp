#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace Desert::Geometry
{
    // Greedy meshing for a sparse voxel volume: merges coplanar, adjacent, equally-exposed faces into as
    // few axis-aligned quads as possible. A blockout wall that is 20x8 cells becomes ONE quad instead of
    // 160 — the same silhouette with a fraction of the triangles, which is what makes a CubeGrid bake
    // usable as real geometry.
    //
    // The volume itself is not modelled here: the caller answers "is this face exposed?" (it owns the
    // rules — neighbours in other layers, deformed cells, material ids), and this only does the merging.
    //
    // Face indices match the CubeGrid mesher's table:
    //   0 = +Z, 1 = -Z, 2 = +Y, 3 = -Y, 4 = -X, 5 = +X.

    inline constexpr glm::ivec3 kVoxelFaceNormal[6] = { { 0, 0, 1 },  { 0, 0, -1 }, { 0, 1, 0 },
                                                        { 0, -1, 0 }, { -1, 0, 0 }, { 1, 0, 0 } };

    // The axis a face's normal runs along, and the two in-plane axes the merge extends over.
    struct VoxelFaceAxes
    {
        int Normal = 2; // 0 = x, 1 = y, 2 = z
        int U      = 0;
        int V      = 1;
    };

    inline VoxelFaceAxes FaceAxes( int face )
    {
        switch ( face )
        {
            case 0:
            case 1:
                return { 2, 0, 1 }; // +Z / -Z: extend over x, y
            case 2:
            case 3:
                return { 1, 0, 2 }; // +Y / -Y: extend over x, z
            default:
                return { 0, 2, 1 }; // -X / +X: extend over z, y
        }
    }

    // One merged run of faces. It covers SizeU x SizeV cells starting at Cell, along the face's own U/V
    // axes (see FaceAxes) — so a quad's world corners are the cell's corners stretched by those counts.
    struct VoxelFaceQuad
    {
        glm::ivec3 Cell{ 0 };
        int        Face  = 0;
        int        SizeU = 1;
        int        SizeV = 1;
    };

    // Merges the exposed faces of @p cells. @p exposed( cell, face ) decides which faces exist at all, and
    // @p mergeKey( cell, face ) returns an id that must MATCH for two faces to merge (material, corner
    // deformation, anything the caller must not blend across) — return 0 everywhere for "merge freely".
    //
    // Deterministic: cells are visited in sorted order, so the same volume always meshes identically.
    template <class ExposedFn, class MergeKeyFn>
    std::vector<VoxelFaceQuad> GreedyMeshFaces( const std::vector<glm::ivec3>& cells, ExposedFn&& exposed,
                                                MergeKeyFn&& mergeKey )
    {
        std::vector<VoxelFaceQuad> quads;
        if ( cells.empty() )
            return quads;

        for ( int face = 0; face < 6; ++face )
        {
            const VoxelFaceAxes ax = FaceAxes( face );

            // Slice the volume along the face's normal; a merge can only ever happen inside one slice.
            // std::map keeps the slices (and each slice's cells) in a fixed order -> deterministic output.
            std::map<int, std::map<std::pair<int, int>, uint64_t>> slices;
            for ( const glm::ivec3& c : cells )
            {
                if ( !exposed( c, face ) )
                    continue;
                slices[c[ax.Normal]][{ c[ax.U], c[ax.V] }] = mergeKey( c, face );
            }

            for ( auto& [sliceCoord, mask] : slices )
            {
                // Classic greedy rectangle growth: run along U while the neighbour is present and shares
                // the merge key, then try to extend the whole run along V one row at a time.
                while ( !mask.empty() )
                {
                    const auto     start = mask.begin();
                    const int      u0    = start->first.first;
                    const int      v0    = start->first.second;
                    const uint64_t key   = start->second;

                    int width = 1;
                    for ( ;; ++width )
                    {
                        const auto it = mask.find( { u0 + width, v0 } );
                        if ( it == mask.end() || it->second != key )
                            break;
                    }

                    int height = 1;
                    for ( ;; ++height )
                    {
                        bool rowFits = true;
                        for ( int u = u0; u < u0 + width && rowFits; ++u )
                        {
                            const auto it = mask.find( { u, v0 + height } );
                            rowFits       = it != mask.end() && it->second == key;
                        }
                        if ( !rowFits )
                            break;
                    }

                    for ( int v = v0; v < v0 + height; ++v )
                        for ( int u = u0; u < u0 + width; ++u )
                            mask.erase( { u, v } );

                    VoxelFaceQuad q;
                    q.Cell[ax.Normal] = sliceCoord;
                    q.Cell[ax.U]      = u0;
                    q.Cell[ax.V]      = v0;
                    q.Face            = face;
                    q.SizeU           = width;
                    q.SizeV           = height;
                    quads.push_back( q );
                }
            }
        }

        return quads;
    }

    // Convenience overload for volumes with nothing to keep apart (every mergeable face is equal).
    template <class ExposedFn>
    std::vector<VoxelFaceQuad> GreedyMeshFaces( const std::vector<glm::ivec3>& cells, ExposedFn&& exposed )
    {
        return GreedyMeshFaces( cells, std::forward<ExposedFn>( exposed ),
                                []( const glm::ivec3&, int ) -> uint64_t { return 0; } );
    }
} // namespace Desert::Geometry
