#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>

namespace Desert
{
    struct Vertex
    {
        glm::vec3 Position;
      //  glm::vec3 Normal;
      //  glm::vec2 TexCoord;
    };

    struct Index
    {
        uint32_t V1, V2, V3;
    };

    class Mesh final
    {
    public:
        explicit Mesh( const std::string& filename );
        Common::BoolResult Invalidate();

        const std::shared_ptr<Graphic::VertexBuffer>& GetVertexBuffer() const
        {
            return m_VertexBuffer;
        }

        const std::shared_ptr<Graphic::IndexBuffer>& GetIndexBuffer() const
        {
            return m_IndexBuffer;
        }

    private:
        std::shared_ptr<Graphic::VertexBuffer> m_VertexBuffer;
        std::shared_ptr<Graphic::IndexBuffer>  m_IndexBuffer;

        const std::string m_Filename;
    };
} // namespace Desert