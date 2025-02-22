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

        const auto GetFramebuffers() const
        {
            return m_Framebuffers;
        }

        virtual uint32_t GetFramebufferWidth() const  override { return m_Width; }
        virtual uint32_t GetFramebufferHeight() const override { return m_Height; }

        void Release();

        virtual void Use( BindUsage = BindUsage::Bind ) const override;

        virtual Common::BoolResult Resize( uint32_t width, uint32_t height, bool forceRecreate = false ) override;

        /*virtual std::shared_ptr<Image2D> GetColorAttachmentImage( uint32_t index = 0 ) const = 0;
        virtual std::shared_ptr<Image2D> GetDepthAttachmentImage() const                     = 0;*/
    private:
    private:
        FramebufferSpecification m_FramebufferSpecification;

        std::vector<VkFramebuffer> m_Framebuffers;

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
    };
} // namespace Desert::Graphic::API::Vulkan