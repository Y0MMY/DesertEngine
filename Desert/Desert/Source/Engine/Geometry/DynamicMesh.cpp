#include "DynamicMesh.hpp"

namespace Desert
{
    Common::BoolResultWithCodes<Desert::MeshError> DynamicMesh::Invalidate()
    {
        m_VertexBuffer =
             Graphic::VertexBuffer::Create( (void*)m_Vertices.data(), m_Vertices.size() * sizeof( Vertex ) );

        m_IndexBuffer = Graphic::IndexBuffer::Create( m_Indices.data(), m_Indices.size() * sizeof( Index ) );

        m_VertexBuffer->RT_Invalidate();
        m_IndexBuffer->RT_Invalidate();

        return Common::MakeSuccessWithCodes<bool, MeshError>( true );
    }

    void DynamicMesh::Update( const std::vector<Vertex>& vertices, const std::vector<Index>& indices )
    {
        m_Vertices = vertices;
        m_Indices  = indices;

        m_VertexBuffer->SetData( (void*)m_Vertices.data(), m_Vertices.size() * sizeof( Vertex ) );

        m_IndexBuffer->SetData( m_Indices.data(), m_Indices.size() * sizeof( Index ) );
    }

} // namespace Desert