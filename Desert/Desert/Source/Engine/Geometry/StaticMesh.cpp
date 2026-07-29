#include "StaticMesh.hpp"

#include "MeshLOD.hpp"

#include <vector>

namespace Desert
{
    StaticMesh::StaticMesh( const std::vector<Vertex>& vertices, const std::vector<Index>& indices,
                            const std::vector<Submesh>& submeshes )
    {
        // Build the GPU index buffer with a per-submesh LOD chain (base indices + appended LODs). The
        // CPU base indices stay untouched; only the GPU buffer + per-submesh LOD ranges carry the LODs.
        std::vector<Submesh> subs       = submeshes;
        std::vector<Index>   gpuIndices = Geometry::BuildLODIndexBuffer( vertices, indices, subs );
        m_Submeshes                     = std::move( subs );

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
