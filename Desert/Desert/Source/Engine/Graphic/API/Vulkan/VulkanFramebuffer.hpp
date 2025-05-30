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

        virtual uint32_t GetFramebufferWidth() const override
        {
            return m_Width;
        }
        virtual uint32_t GetFramebufferHeight() const override
        {
            return m_Height;
        }

        const auto GetVKFramebuffer() const
        {
            return m_Framebuffer;
        }

        const auto GetVKRenderPass() const
        {
            return m_RenderPass;
        }

        Common::BoolResult Invalidate() override
        {
            return Common::MakeError( "Use Resize()" );
        }
        Common::BoolResult Release() override;

        virtual void Use( BindUsage = BindUsage::Bind ) const override;

        virtual Common::BoolResult Resize( uint32_t width, uint32_t height, bool forceRecreate = false ) override;

        virtual std::shared_ptr<Image2D> GetColorAttachmentImage( uint32_t index = 0 ) const override
        {
            return m_ColorAttachment;
        }
        virtual std::shared_ptr<Image2D> GetDepthAttachmentImage() const override
        {
            return m_DepthAttachment;
        }

    private:
        Common::Result<VkFramebuffer> CreateFramebuffer( VkDevice device, uint32_t width, uint32_t height );

    private:
        std::shared_ptr<Image2D> m_DepthAttachment;
        std::shared_ptr<Image2D> m_ColorAttachment;

        FramebufferSpecification m_FramebufferSpecification;

        VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
        VkRenderPass  m_RenderPass  = VK_NULL_HANDLE;

        uint32_t m_Width  = 0;
        uint32_t m_Height = 0;
    };
} // namespace Desert::Graphic::API::Vulkan