#pragma once

#include <Common/Core/Math/AABB.hpp>

#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>

#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert
{
    struct Vertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec3 Tangent;
        glm::vec3 Bitangent;
        glm::vec2 TexCoord;
    };

    struct Index
    {
        uint32_t V1, V2, V3;
    };

    struct TriangleCache
    {
        Vertex V1, V2, V3;
    };

    struct Submesh
    {
        std::string Name;
        uint32_t    VertexOffset; // Offsetting vertices in the main buffer
        uint32_t    VertexCount;
        uint32_t    IndexOffset;
        uint32_t    IndexCount;

        glm::mat4 Transform;

        Common::Math::AABB BoundingBox;
    };

    class Mesh final
    {
    public:
        explicit Mesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset );

        Common::BoolResult Invalidate();

        [[nodiscard]] const auto& GetSubmeshes() const
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

        [[nodiscard]] const auto& GetAsset() const
        {
            return m_MeshAsset;
        }

        [[nodiscard]] const auto& GetFilepath() const
        {
            return m_Filepath;
        }

    private:
        std::shared_ptr<Graphic::VertexBuffer> m_VertexBuffer;
        std::shared_ptr<Graphic::IndexBuffer>  m_IndexBuffer;

        std::vector<std::vector<TriangleCache>> m_TriangleCache; // [SubmeshIndex][TriangleIndex]
        std::vector<Submesh>                    m_Submeshes;

        const std::weak_ptr<Assets::MeshAsset> m_MeshAsset;
        const Common::Filepath                 m_Filepath;
    };
} // namespace Desert