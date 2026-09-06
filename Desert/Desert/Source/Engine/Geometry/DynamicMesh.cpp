#include "DynamicMesh.hpp"

#include "MeshLOD.hpp"

namespace Desert
{
    Common::BoolResultWithCodes<Desert::MeshError> DynamicMesh::Invalidate()
    {
        m_VertexBuffer =
             Graphic::VertexBuffer::Create( (void*)m_Vertices.data(), m_Vertices.size() * sizeof( Vertex ) );
        const auto vertices = m_VertexBuffer->RT_Invalidate();
        if ( !vertices.IsSuccess() )
            return Common::MakeErrorWithCodes<bool, MeshError>( { MeshError::GpuUploadFailed },
                                                                vertices.GetError() );

        if ( !m_Indices.empty() )
        {
            // With LODs, upload base + appended LOD indices (fills each submesh's LOD ranges) while
            // GetIndices() stays the base geometry; otherwise upload the base indices as-is.
            if ( m_GenerateLODs )
            {
                const std::vector<Index> gpu =
                     Geometry::BuildLODIndexBuffer( m_Vertices, m_Indices, m_Submeshes );
                m_IndexBuffer = Graphic::IndexBuffer::Create( gpu.data(), gpu.size() * sizeof( Index ) );
            }
            else
            {
                m_IndexBuffer =
                     Graphic::IndexBuffer::Create( m_Indices.data(), m_Indices.size() * sizeof( Index ) );
            }
            const auto uploaded = m_IndexBuffer->RT_Invalidate();
            if ( !uploaded.IsSuccess() )
                return Common::MakeErrorWithCodes<bool, MeshError>( { MeshError::GpuUploadFailed },
                                                                    uploaded.GetError() );
        }
        else
        {
            m_IndexBuffer = nullptr;
        }

        return Common::MakeSuccessWithCodes<bool, MeshError>( true );
    }

    void DynamicMesh::Update( const std::vector<Vertex>& vertices, const std::vector<Index>& indices )
    {
        m_Vertices = vertices;
        m_Indices  = indices;

        if ( !m_VertexBuffer || m_Vertices.size() * sizeof( Vertex ) > m_VertexBuffer->GetSize() )
        {
            m_VertexBuffer =
                 Graphic::VertexBuffer::Create( (void*)m_Vertices.data(), m_Vertices.size() * sizeof( Vertex ) );
            // LOGGED HERE RATHER THAN RETURNED, and the asymmetry with Invalidate above is deliberate.
            // Update is called once per mouse-move while a vertex is being dragged (PolyEditTool), and its
            // single caller is inside an ImGui interaction that has nowhere to put a failure — it can
            // neither undo the edit nor stop the drag meaningfully. What was missing was not a channel but
            // a REPORT: before this, a reallocation that failed left the mesh drawing its previous
            // contents and the edit simply did not appear.
            const auto uploaded = m_VertexBuffer->RT_Invalidate();
            if ( !uploaded.IsSuccess() )
                LOG_ERROR( "[DynamicMesh] vertex buffer re-upload failed, the edit is not on the GPU: {}",
                           uploaded.GetError() );
        }
        else
        {
            m_VertexBuffer->SetData( (void*)m_Vertices.data(), m_Vertices.size() * sizeof( Vertex ) );
        }

        if ( !m_Indices.empty() )
        {
            if ( !m_IndexBuffer || m_Indices.size() * sizeof( Index ) > m_IndexBuffer->GetSize() )
            {
                m_IndexBuffer = Graphic::IndexBuffer::Create( m_Indices.data(), m_Indices.size() * sizeof( Index ) );
                const auto uploaded = m_IndexBuffer->RT_Invalidate(); // reported, not returned; see above
                if ( !uploaded.IsSuccess() )
                    LOG_ERROR( "[DynamicMesh] index buffer re-upload failed, the edit is not on the GPU: {}",
                               uploaded.GetError() );
            }
            else
            {
                m_IndexBuffer->SetData( m_Indices.data(), m_Indices.size() * sizeof( Index ) );
            }
        }
        else
        {
            m_IndexBuffer = nullptr;
        }
    }

    void DynamicMesh::Flatten()
    {
        if ( m_Indices.empty() )
            return;

        std::vector<Vertex> newVertices;
        newVertices.reserve( m_Indices.size() * 3 );

        for ( const auto& index : m_Indices )
        {
            newVertices.push_back( m_Vertices[index.V1] );
            newVertices.push_back( m_Vertices[index.V2] );
            newVertices.push_back( m_Vertices[index.V3] );
        }

        m_Vertices = std::move( newVertices );
        m_Indices.clear();

        // Update submeshes to reflect new vertex counts and lack of indices
        uint32_t vertexOffset = 0;
        for ( auto& submesh : m_Submeshes )
        {
            // This is a bit naive as it assumes submeshes were partitioning the indices
            // In a real scenario, we might need to track which submesh each index belonged to.
            // For now, let's assume one submesh or simple linear split.
            uint32_t triangleCount = submesh.IndexCount / 3;
            submesh.VertexCount    = triangleCount * 3;
            submesh.VertexOffset   = vertexOffset;
            submesh.IndexCount     = 0;
            submesh.IndexOffset    = 0;

            vertexOffset += submesh.VertexCount;
        }

        Invalidate();
    }

} // namespace Desert