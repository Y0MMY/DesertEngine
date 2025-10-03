#pragma once

#include <Common/Core/ResultStr.hpp>

#include <Engine/Graphic/Shader.hpp>
#include "BaseBuffer.hpp"

namespace Desert::Uniforms
{
    class StorageBuffer : public BaseBuffer
    {
    public:
        virtual ~StorageBuffer() = default;

        virtual const std::vector<Core::Models::Common::Field>& GetFields() const override
        {
            return {};
        }

    private:
        static std::shared_ptr<StorageBuffer> Create( const std::string_view debugName, uint32_t size,
                                                      uint32_t binding );

        friend class UniformManager;
    };

} // namespace Desert::Uniforms