#pragma once

#include <Common/Core/Result.hpp>

#include <Engine/Graphic/Shader.hpp>
#include "BaseBuffer.hpp"

namespace Desert::Uniforms
{
    class UniformBuffer : public BaseBuffer
    {
    public:
        virtual ~UniformBuffer() = default;

    private:
        static std::shared_ptr<UniformBuffer> Create( const std::string_view debugName, uint32_t size,
                                                      uint32_t binding );

        friend class UniformManager;
    };

} // namespace Desert::Uniforms