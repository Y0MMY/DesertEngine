#include "MeshLOD.hpp"

#include "MeshSimplifier.hpp"

#include <cstdint>

namespace Desert::Geometry
{
    namespace
    {
        // Ratios for the auto LOD chain (LOD0 = original). A level that cannot beat the previous one
        // reuses that range so LOD selection stays monotonic.
        constexpr float kLODRatios[] = { 0.5f, 0.25f, 0.1f };
    } // namespace

    std::vector<Index> BuildLODIndexBuffer( const std::vector<Vertex>& vertices,
                                            const std::vector<Index>&  baseIndices,
                                            std::vector<Submesh>&      submeshes )
    {
        std::vector<Index> gpuIndices = baseIndices;

        for ( auto& sm : submeshes )
        {
            sm.LODs.clear();
            sm.LODs.push_back( { sm.IndexOffset, sm.IndexCount } ); // LOD0 = original

            const uint32_t triCount = sm.IndexCount / 3;
            if ( triCount < 8 || sm.VertexCount == 0 )
                continue; // too small to bother

            // Submesh-local flat indices + tightly-packed positions (indices are relative to VertexOffset,
            // so LOD indices reuse the same baseVertex).
            std::vector<uint32_t> flat;
            flat.reserve( triCount * 3 );
            const uint32_t triStart = sm.IndexOffset / 3;
            for ( uint32_t t = 0; t < triCount; ++t )
            {
                const Index& idx = baseIndices[triStart + t];
                flat.push_back( idx.V1 );
                flat.push_back( idx.V2 );
                flat.push_back( idx.V3 );
            }
            std::vector<float> pos;
            pos.reserve( sm.VertexCount * 3 );
            for ( uint32_t v = 0; v < sm.VertexCount; ++v )
            {
                const auto& p = vertices[sm.VertexOffset + v].Position;
                pos.push_back( p.x );
                pos.push_back( p.y );
                pos.push_back( p.z );
            }

            for ( float ratio : kLODRatios )
            {
                const auto lod = SimplifyMesh( pos.data(), sm.VertexCount, flat, ratio );
                if ( lod.Indices.size() < 3 || lod.Indices.size() >= sm.LODs.back().IndexCount )
                {
                    sm.LODs.push_back( sm.LODs.back() );
                    continue;
                }
                const uint32_t offset = static_cast<uint32_t>( gpuIndices.size() * 3 );
                for ( std::size_t i = 0; i + 2 < lod.Indices.size(); i += 3 )
                    gpuIndices.push_back( { lod.Indices[i], lod.Indices[i + 1], lod.Indices[i + 2] } );
                sm.LODs.push_back( { offset, static_cast<uint32_t>( lod.Indices.size() ) } );
            }
        }

        return gpuIndices;
    }
} // namespace Desert::Geometry
