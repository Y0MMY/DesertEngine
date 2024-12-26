#pragma once

#include <Engine/Graphic/RendererTypes.hpp>

namespace Desert::Graphic
{
    enum class ShaderDataType
    {
        None = 0,
        Float,
        Float2,
        Float3,
        Float4,
        Int,
        Int2,
        Int3,
        Int4,
        Bool
    };

    inline uint32_t ShaderDataTypeSize( ShaderDataType type )
    {
        switch ( type )
        {
            case ShaderDataType::Float:
            case ShaderDataType::Int:
                return 4;

            case ShaderDataType::Float2:
            case ShaderDataType::Int2:
                return 4 * 2;

            case ShaderDataType::Float3:
            case ShaderDataType::Int3:
                return 4 * 3;

            case ShaderDataType::Float4:
            case ShaderDataType::Int4:
                return 4 * 4;

            case ShaderDataType::Bool:
                return 1;
        }
    }

    struct VertexBufferElement
    {
        ShaderDataType Type;
        std::string    Name;
        std::uint32_t  Offset;
        std::uint32_t  Size;
        bool           Normalized;

        VertexBufferElement() = default;

        VertexBufferElement( ShaderDataType type, const std::string& name, bool normalized = false )
             : Type( type ), Name( name ), Offset( 0 ), Size( ShaderDataTypeSize( type ) ),
               Normalized( normalized )
        {
        }

        uint32_t GetComponentCount() const;
    };

    class VertexBufferLayout
    {
    public:
        VertexBufferLayout()
        {
        }
        VertexBufferLayout( const std::initializer_list<VertexBufferElement>& elements ) : m_Elements( elements )
        {
            CalculateOffsetsAndStride();
        }

        inline uint32_t GetStride() const
        {
            return m_Stride;
        }
        inline const std::vector<VertexBufferElement>& GetElements() const
        {
            return m_Elements;
        }

        uint32_t GetElementCount() const
        {
            return (uint32_t)m_Elements.size();
        }

        std::vector<VertexBufferElement>::iterator begin()
        {
            return m_Elements.begin();
        }
        std::vector<VertexBufferElement>::iterator end()
        {
            return m_Elements.end();
        }
        std::vector<VertexBufferElement>::const_iterator begin() const
        {
            return m_Elements.begin();
        }
        std::vector<VertexBufferElement>::const_iterator end() const
        {
            return m_Elements.end();
        }

    private:
        void CalculateOffsetsAndStride()
        {
            std::uint32_t offset = 0;
            m_Stride             = 0;
            for ( auto& element : m_Elements )
            {
                element.Offset = offset;
                offset += element.Size;
                m_Stride += element.Size;
            }
        }

        std::vector<VertexBufferElement> m_Elements;
        std::uint32_t                    m_Stride = 0;
    };

    class VertexBuffer
    {
    public:
        virtual ~VertexBuffer()                                                = default;
        virtual void SetData( void* data, uint32_t size, uint32_t offset = 0 ) = 0;
        virtual void Use( BindUsage use = BindUsage::Bind ) const              = 0;
        virtual void RT_Use( BindUsage use = BindUsage::Bind ) const           = 0;

        virtual [[nodiscard]] unsigned int GetSize() const = 0;

        static std::shared_ptr<VertexBuffer> Create( void* data, uint32_t size,
                                                     BufferUsage usage = BufferUsage::Static );
        static std::shared_ptr<VertexBuffer> Create( uint32_t size, BufferUsage usage = BufferUsage::Dynamic );
    };
} // namespace Desert::Graphic