#pragma once

#include <Engine/Graphic/RendererTypes.hpp>
#include <Engine/Graphic/DynamicResources.hpp>

namespace Desert::Graphic
{
    class IndexBuffer : public DynamicResources
    {
    public:
        virtual ~IndexBuffer()                                       = default;
        virtual void SetData()                                       = 0;
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