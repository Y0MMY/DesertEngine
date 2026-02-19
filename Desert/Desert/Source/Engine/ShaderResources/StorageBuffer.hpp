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

    private:
        static std::shared_ptr<StorageBuffer> Create( const std::string_view debugName, uint32_t size,
                                                      uint32_t binding );

        friend class ShaderResourcesManager;
    };

} // namespace Desert::ShaderResources