#pragma once

#include <Engine/Graphic/Framebuffer.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanFramebuffer : public Framebuffer
    {
    public:
        VulkanFramebuffer( const FramebufferSpecification& spec );
        virtual ~VulkanFramebuffer() = default;

        virtual const FramebufferSpecification GetSpecification() const override
        {
            return m_FramebufferSpecification;
        }

        const auto GetRenderPass() const
        {
            return m_VkRenderPass;
        }

        const auto GetFramebuffers() const
        {
            return m_Framebuffers;
        }

        virtual void Use( BindUsage = BindUsage::Bind ) const override;

        virtual Common::BoolResult Resize( uint32_t width, uint32_t height, bool forceRecreate = false ) override;

        /*virtual std::shared_ptr<Image2D> GetColorAttachmentImage( uint32_t index = 0 ) const = 0;
        virtual std::shared_ptr<Image2D> GetDepthAttachmentImage() const                     = 0;*/
    private:
        FramebufferSpecification m_FramebufferSpecification;

        VkRenderPass m_VkRenderPass;

        std::vector<VkFramebuffer> m_Framebuffers;
    };
} // namespace Desert::Graphic::API::Vulkan