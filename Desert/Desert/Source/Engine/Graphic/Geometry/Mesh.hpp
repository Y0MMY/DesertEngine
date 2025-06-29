#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>

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
    };

    class Mesh final
    {
    public:
        explicit Mesh( const std::string& filename );

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

    private:
        std::shared_ptr<Graphic::VertexBuffer> m_VertexBuffer;
        std::shared_ptr<Graphic::IndexBuffer>  m_IndexBuffer;

        std::vector<std::vector<TriangleCache>> m_TriangleCache; // [SubmeshIndex][TriangleIndex]
        std::vector<Submesh>                    m_Submeshes;

        const std::string m_Filename;
    };
} // namespace Desert