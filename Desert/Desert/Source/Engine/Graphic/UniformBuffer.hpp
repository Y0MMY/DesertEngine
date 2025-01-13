#pragma once

namespace Desert::Graphic
{
    class UniformBuffer
    {
    public:
        virtual ~UniformBuffer() = default;

        virtual void SetData( const void* data, uint32_t size, uint32_t offset = 0 )    = 0;
        virtual void RT_SetData( const void* data, uint32_t size, uint32_t offset = 0 ) = 0;

        virtual uint32_t GetBinding() const = 0;

        static std::shared_ptr<UniformBuffer> Create( uint32_t size, uint32_t binding );
    };
} // namespace Desert::Graphic