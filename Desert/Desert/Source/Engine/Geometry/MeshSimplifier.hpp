#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Desert::Geometry
{
    // Thin wrapper over meshoptimizer for LOD generation. Simplifies an index buffer to ~targetRatio of
    // its triangles while preserving the mesh silhouette (meshopt_simplify). The LOD reuses the SAME
    // vertex buffer — only extra indices are produced — so a full LOD chain costs little memory.
    struct SimplifyResult
    {
        std::vector<uint32_t> Indices;
        float                 Error = 0.0f; // resulting geometric error, relative to the mesh extent
    };

    // positions: tightly-packed xyz per vertex (vertexCount * 3 floats). targetRatio in (0,1]; targetError
    // is the max allowed relative error (the simplifier stops early if it would exceed it). Returns the
    // original indices unchanged for degenerate input.
    SimplifyResult SimplifyMesh( const float* positions, std::size_t vertexCount,
                                 const std::vector<uint32_t>& indices, float targetRatio,
                                 float targetError = 0.05f );
} // namespace Desert::Geometry
