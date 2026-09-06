#pragma once

#include <Engine/Graphic/RendererTypes.hpp>
#include <Engine/Graphic/DynamicResources.hpp>

// For DESERT_VERIFY in ShaderDataTypeSize below. Not implicit: this header is reached from
// Geometry/Mesh.hpp by translation units that never include Core.hpp on their own.
#include <Common/Core/Core.hpp>

#include <vector>

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

            // `None` is the enum's ZERO, so a default-constructed VertexBufferElement carries it. It
            // was the one value with no case and no fallthrough return, which made the whole function
            // fall off its end — undefined behaviour returning whatever the ABI's return register
            // happened to hold, straight into a vertex layout's stride.
            case ShaderDataType::None:
                break;
        }

        // An invariant, not an error channel: the only caller is VertexBufferElement's constructor and
        // every layout in the engine is written by hand with a literal type, so `None` here means
        // engine code built a layout out of an unset attribute. Same treatment the unreachable
        // renderer-API cases in VertexBuffer.cpp already get. Returning 0 quietly would have handed
        // back a zero-stride layout, which is the silent emptiness this project forbids.
        DESERT_VERIFY( false, "ShaderDataTypeSize: ShaderDataType::None has no size — a vertex layout "
                              "was built from an unset attribute type" );
        return 0;
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

    class VertexBuffer : public DynamicResources
    {
    public:
        virtual ~VertexBuffer()                                                = default;
        virtual void SetData( void* data, uint32_t size, uint32_t offset = 0 ) = 0;
        virtual void Use( BindUsage use = BindUsage::Bind ) const              = 0;
        virtual void RT_Use( BindUsage use = BindUsage::Bind ) const           = 0;

        [[nodiscard]] virtual unsigned int GetSize() const = 0;

        [[nodiscard]] virtual Common::BoolResultStr RT_Invalidate() = 0;

        static std::shared_ptr<VertexBuffer> Create( void* data, uint32_t size,
                                                     BufferUsage usage = BufferUsage::Static );
        static std::shared_ptr<VertexBuffer> Create( uint32_t size, BufferUsage usage = BufferUsage::Dynamic );
    };
} // namespace Desert::Graphic