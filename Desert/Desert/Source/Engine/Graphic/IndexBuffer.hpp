#pragma once

#include <Engine/Graphic/RendererTypes.hpp>
#include <Engine/Graphic/DynamicResources.hpp>

// For NO_DISCARD on SetData below. Not implicit: this header is reached from Geometry/Mesh.hpp by
// translation units that never include Core.hpp on their own — the same reason VertexBuffer.hpp
// includes it explicitly.
#include <Common/Core/Core.hpp>

namespace Desert::Graphic
{
    class IndexBuffer : public DynamicResources
    {
    public:
        virtual ~IndexBuffer() = default;

        /// Overwrite @p size bytes at @p offset. Refuses, having written nothing, when the buffer is not
        /// dynamic, when its memory is not mapped, or when the range does not fit. See
        /// VertexBuffer::SetData for why this answers instead of returning `void`.
        NO_DISCARD virtual Common::BoolResultStr SetData( void* data, uint32_t size, uint32_t offset = 0 ) = 0;

        virtual void Use( BindUsage use = BindUsage::Bind ) const    = 0;
        virtual void RT_Use( BindUsage use = BindUsage::Bind ) const = 0;

        virtual unsigned int GetSize() const  = 0;
        virtual unsigned int GetCount() const = 0;

        [[nodiscard]] virtual Common::BoolResultStr RT_Invalidate() = 0;

        static std::shared_ptr<IndexBuffer> Create( const void* data, uint32_t size,
                                                    BufferUsage usage = BufferUsage::Static );
        static std::shared_ptr<IndexBuffer> Create( uint32_t size, BufferUsage usage = BufferUsage::Dynamic );
    };
} // namespace Desert::Graphic