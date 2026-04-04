#pragma once

#include <Common/Core/Math/AABB.hpp>
#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>
#include "Errors/MeshError.hpp"
#include "MeshTypes.hpp"

#include <Common/Core/ResultWithCodes.hpp>

#include <unordered_map>

namespace Desert::Animation
{
    class Skeleton;
    struct BoneInfo;
} // namespace Desert::Animation

namespace Desert
{
    class Mesh
    {
    public:
        virtual ~Mesh() = default;

        // Common interface
        [[nodiscard]] virtual MeshType GetType() const = 0;
        [[nodiscard]] bool             IsSkinned() const
        {
            return GetType() == MeshType::Skinned;
        }

        [[nodiscard]] virtual const std::vector<Submesh>& GetSubmeshes() const
        {
            return m_Submeshes;
        }
        [[nodiscard]] const std::shared_ptr<Graphic::VertexBuffer>& GetVertexBuffer() const
        {
            return m_VertexBuffer;
        }
        [[nodiscard]] const std::shared_ptr<Graphic::IndexBuffer>& GetIndexBuffer() const
        {
            return m_IndexBuffer;
        }

        [[nodiscard]] virtual Common::BoolResultWithCodes<MeshError> Invalidate() = 0;

    protected:
        // Protected data accessible by derived classes
        std::shared_ptr<Graphic::VertexBuffer>  m_VertexBuffer;
        std::shared_ptr<Graphic::IndexBuffer>   m_IndexBuffer;
        std::vector<std::vector<TriangleCache>> m_TriangleCache;
        std::vector<Submesh>                    m_Submeshes;
    };

    class StaticMesh : public Mesh
    {
    public:
        StaticMesh( const std::vector<Vertex>& vertices, const std::vector<Index>& indices,
                    const std::vector<Submesh>& submeshes );

        [[nodiscard]] MeshType GetType() const override
        {
            return MeshType::Static;
        }

        [[nodiscard]] virtual Common::BoolResultWithCodes<MeshError> Invalidate() override;
    };

    class SkinnedMesh : public Mesh
    {
    public:
        SkinnedMesh( const std::vector<SkinnedVertex>& vertices, const std::vector<Index>& indices,
                     const std::vector<Submesh>& submeshes, const Animation::Skeleton* skeleton );

        MeshType GetType() const override
        {
            return MeshType::Skinned;
        }

        const Animation::Skeleton& GetSkeleton() const
        {
            return *m_Skeleton;
        }

        [[nodiscard]] virtual Common::BoolResultWithCodes<MeshError> Invalidate() override;

    private:
        const Animation::Skeleton*                m_Skeleton;
        std::unordered_map<std::string, uint32_t> m_BoneNameToIndex;
    };
} // namespace Desert
