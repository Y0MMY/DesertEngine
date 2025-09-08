#pragma once

#include <Common/Core/Result.hpp>

namespace Desert::Uniforms
{
    class BaseBuffer
    {
    public:
        virtual ~BaseBuffer() = default;

        virtual void SetData( const void* data, uint32_t size, uint32_t offset = 0 )    = 0;
        virtual void RT_SetData( const void* data, uint32_t size, uint32_t offset = 0 ) = 0;

        virtual const uint32_t GetBinding() const = 0;
        virtual const uint32_t GetSize() const    = 0;

        virtual const void* GetData() const = 0;
    };

} // namespace Desert::Uniforms