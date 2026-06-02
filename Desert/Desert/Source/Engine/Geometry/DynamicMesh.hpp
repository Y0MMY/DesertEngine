#pragma once

#include "Mesh.hpp"

namespace Desert
{
    class DynamicMesh : public Mesh
    {
    public:
        DynamicMesh( const std::vector<Vertex>& vertices, const std::vector<Index>& indices,
                     const std::vector<Submesh>& submeshes )
             : m_Vertices( vertices ), m_Indices( indices )
        {
            m_Submeshes = submeshes;
        }

        MeshType GetType() const override
        {
            return MeshType::Static;
        }

        Common::BoolResultWithCodes<MeshError> Invalidate() override;

        void Update( const std::vector<Vertex>& vertices, const std::vector<Index>& indices );

        void Flatten();

        [[nodiscard]] std::vector<Vertex>&       GetVertices() { return m_Vertices; }
        [[nodiscard]] const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
        [[nodiscard]] std::vector<Index>&        GetIndices() { return m_Indices; }
        [[nodiscard]] const std::vector<Index>&  GetIndices() const { return m_Indices; }

    private:
        std::vector<Vertex> m_Vertices;
        std::vector<Index>  m_Indices;
    };
} // namespace Desert
