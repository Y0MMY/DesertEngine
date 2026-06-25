#pragma once

#include <Common/Core/ResultStr.hpp>

#include <Engine/Graphic/Shader.hpp>
#include "BaseBuffer.hpp"

namespace Desert::ShaderResources
{
    class StorageBuffer : public BaseBuffer
    {
    public:
        virtual ~StorageBuffer() = default;

        virtual const std::vector<ShaderLayout::ShaderFieldLayout>& GetFields() const override
        {
            return {};
        }

        // Allocate a storage buffer of an explicit size. Public so systems (e.g. the auto-exposure
        // histogram) can create one directly — the reflection-driven path in ShaderResourcesManager
        // hardcodes a 36-byte size and is only suitable for shader-declared buffers.
        static std::shared_ptr<StorageBuffer> Create( const std::string_view debugName, uint32_t size,
                                                      uint32_t binding );
    };

} // namespace Desert::ShaderResources