#pragma once

#include <Engine/Graphic/Framebuffer.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanFramebuffer : public Framebuffer
    {
    public:
        VulkanFramebuffer( const FramebufferSpecification& spec );
        virtual ~VulkanFramebuffer();

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

        uint32_t GetColorAttachmentCount() const override
        {
            return m_ColorAttachments.size() + m_ExternalColorAttachments.size();
        }

        uint32_t GetDepthAttachmentCount() const override
        {
            return ( m_DepthAttachment ||
                     ( m_ExternalDepthAttachment && m_ExternalDepthAttachment->Image.lock() ) )
                        ? 1U
                        : 0U;
        }

        Common::BoolResult Invalidate() override
        {
            return Common::MakeError( "Use Resize()" );
        }
        Common::BoolResult Release() override;

        virtual void Use( BindUsage = BindUsage::Bind ) const override
        {
        }

        virtual Common::BoolResult Resize( uint32_t width, uint32_t height, bool forceRecreate = false ) override;

        virtual std::shared_ptr<Image2D> GetColorAttachmentImage( uint32_t index = 0 ) const override
        {
            return m_ColorAttachments.at( index );
        }

        virtual std::shared_ptr<Image2D> GetDepthAttachmentImage() const override
        {
            return m_DepthAttachment;
        }

        const auto& GetClearValues() const
        {
            return m_ClearValues;
        }

    private:
        Common::Result<VkFramebuffer> CreateFramebuffer( VkDevice device, uint32_t width, uint32_t height );

    private:
        std::shared_ptr<Image2D>              m_DepthAttachment;
        std::vector<std::shared_ptr<Image2D>> m_ColorAttachments;

        struct ExternalAttachmentInfo
        {
            std::weak_ptr<Image2D> Image;
            AttachmentLoad         LoadOp;
        };

        std::optional<ExternalAttachmentInfo> m_ExternalDepthAttachment;
        std::vector<ExternalAttachmentInfo>   m_ExternalColorAttachments;

        FramebufferSpecification m_FramebufferSpecification;

        VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
        VkRenderPass  m_RenderPass  = VK_NULL_HANDLE;

        uint32_t m_Width  = 0;
        uint32_t m_Height = 0;

        std::vector<VkClearValue> m_ClearValues;
    };
} // namespace Desert::Graphic::API::Vulkan