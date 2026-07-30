#include "SkinnedMesh.hpp"

#include <Engine/Animation/Skeleton.hpp>
#include <Engine/Animation/BoneInfo.hpp>

namespace Desert
{

    SkinnedMesh::SkinnedMesh( const std::vector<SkinnedVertex>& vertices, const std::vector<Index>& indices,
                              const std::vector<Submesh>& submeshes, const Animation::Skeleton* skeleton )
         : m_Skeleton( skeleton )
    {
        m_Submeshes = submeshes;
        m_Vertices  = vertices; // retained on the CPU for viewport picking (posed-AABB)

        m_VertexBuffer =
             Graphic::VertexBuffer::Create( (void*)vertices.data(), vertices.size() * sizeof( SkinnedVertex ) );

        m_IndexBuffer = Graphic::IndexBuffer::Create( indices.data(), indices.size() * sizeof( Index ) );
    }

    Common::BoolResultWithCodes<Desert::MeshError> SkinnedMesh::Invalidate()
    {
        m_VertexBuffer->RT_Invalidate();
        m_IndexBuffer->RT_Invalidate();

        return Common::MakeSuccessWithCodes<bool, MeshError>( true );
    }

} // namespace Desert