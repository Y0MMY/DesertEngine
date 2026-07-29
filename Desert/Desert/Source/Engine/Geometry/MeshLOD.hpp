#pragma once

#include "MeshTypes.hpp"

#include <vector>

namespace Desert::Geometry
{
    // Builds the GPU index buffer for a mesh WITH a LOD chain: it copies @p baseIndices, then for each
    // submesh appends simplified LOD index sets (meshopt) and records the per-submesh ranges in
    // Submesh::LODs (LODs[0] = the original range). Returns the concatenated buffer (base + LODs). The
    // CPU-side base indices are NOT modified, so serialization/collision keep the original geometry.
    std::vector<Index> BuildLODIndexBuffer( const std::vector<Vertex>& vertices,
                                            const std::vector<Index>&  baseIndices,
                                            std::vector<Submesh>&      submeshes );
} // namespace Desert::Geometry
