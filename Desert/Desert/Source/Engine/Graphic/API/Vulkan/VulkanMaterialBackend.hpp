#pragma once

#include <Engine/Graphic/Materials/MaterialBackend.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanMaterialBackend final : public MaterialBackend
    {
    public:
        using MaterialBackend::MaterialBackend;

        virtual void ApplyUniformBuffer( MaterialProperty* prop ) override;
        virtual void ApplyTexture2D( MaterialProperty* prop ) override;
        virtual void ApplyTextureCube( MaterialProperty* prop ) override;

        virtual void FlushUpdates() override;

        virtual void ApplyPushConstants( MaterialExecutor* material, Pipeline* pipeline ) override;

    private:
        std::vector<VkWriteDescriptorSet> m_PendingDescriptorWrites;
    };
} // namespace Desert::Graphic::API::Vulkan