#pragma once

#include <Common/Core/Result.hpp>

#include <Engine/Graphic/Shader.hpp>

namespace Desert::Uniforms
{
    class UniformBuffer
    {
    public:
        virtual ~UniformBuffer() = default;

        virtual void SetData( const void* data, uint32_t size, uint32_t offset = 0 )    = 0;
        virtual void RT_SetData( const void* data, uint32_t size, uint32_t offset = 0 ) = 0;

        virtual const uint32_t GetBinding() const = 0;
        virtual const uint32_t GetSize() const    = 0;

        virtual const void* GetData() const = 0;

    private:
        static std::shared_ptr<UniformBuffer> Create( const std::string_view debugName, uint32_t size,
                                                      uint32_t binding );

        friend class UniformManager;
    };
} // namespace Desert::Uniforms