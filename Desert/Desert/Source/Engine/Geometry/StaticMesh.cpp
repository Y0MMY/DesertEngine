#include "StaticMesh.hpp"

#include "MeshSimplifier.hpp"

#include <cstdint>
#include <vector>

namespace Desert
{
    namespace
    {
        // Ratios for the auto LOD chain (LOD0 = original). meshopt_simplify preserves the silhouette;
        // a level that cannot beat the previous one reuses that range so LOD selection is monotonic.
        constexpr float kLODRatios[] = { 0.5f, 0.25f, 0.1f };
    } // namespace

    StaticMesh::StaticMesh( const std::vector<Vertex>& vertices, const std::vector<Index>& indices,
                            const std::vector<Submesh>& submeshes )
    {
        // Start the GPU index buffer as the original indices, then APPEND simplified LOD indices per
        // submesh. The CPU-side base indices (Mesh::GetIndices — used by custom-mesh serialization /
        // collision) are left untouched; LODs live only in the GPU buffer + per-submesh ranges.
        std::vector<Index> gpuIndices = indices;

        std::vector<Submesh> subs = submeshes;
        for ( auto& sm : subs )
        {
            sm.LODs.clear();
            sm.LODs.push_back( { sm.IndexOffset, sm.IndexCount } ); // LOD0 = original

            const uint32_t triCount = sm.IndexCount / 3;
            if ( triCount < 8 || sm.VertexCount == 0 )
                continue; // too small to bother

            // Submesh-local flat indices + tightly-packed positions (indices are already relative to
            // VertexOffset, so LOD indices reuse the same baseVertex).
            std::vector<uint32_t> flat;
            flat.reserve( triCount * 3 );
            const uint32_t triStart = sm.IndexOffset / 3;
            for ( uint32_t t = 0; t < triCount; ++t )
            {
                const Index& idx = indices[triStart + t];
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
                const auto lod = Geometry::SimplifyMesh( pos.data(), sm.VertexCount, flat, ratio );
                // No meaningful reduction -> reuse the previous level (keeps the chain monotonic).
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

        m_Submeshes = std::move( subs );

        m_VertexBuffer =
             Graphic::VertexBuffer::Create( (void*)vertices.data(), vertices.size() * sizeof( Vertex ) );
        m_IndexBuffer =
             Graphic::IndexBuffer::Create( gpuIndices.data(), gpuIndices.size() * sizeof( Index ) );
    }

    Common::BoolResultWithCodes<Desert::MeshError> StaticMesh::Invalidate()
    {
        m_VertexBuffer->RT_Invalidate();
        m_IndexBuffer->RT_Invalidate();

        return Common::MakeSuccessWithCodes<bool, MeshError>( true );
    }

} // namespace Desert
