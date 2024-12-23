#pragma once

#include <Engine/Core/Formats/ImageFormat.hpp>
#include <Engine/Graphic/RendererTypes.hpp>

namespace Desert::Graphic
{
    struct FramebufferAttachmentSpecification
    {
        FramebufferAttachmentSpecification() = default;
        FramebufferAttachmentSpecification( const std::initializer_list<Core::Formats::ImageFormat>& attachments )
             : Attachments( attachments )
        {
        }

        std::vector<Core::Formats::ImageFormat> Attachments;
    };

    struct FramebufferSpecification
    {
        uint32_t                           Samples = 2; // Multisampling
        FramebufferAttachmentSpecification Attachments;

        bool NoResizeble = false;
    };

    class Framebuffer
    {
    public:
        virtual ~Framebuffer() = default;

        virtual const FramebufferSpecification GetSpecification() const               = 0;
        virtual void                           Use( BindUsage = BindUsage::Bind ) const = 0;

        virtual Common::BoolResult Resize( uint32_t width, uint32_t height, bool forceRecreate = false ) = 0;

        /*virtual std::shared_ptr<Image2D> GetColorAttachmentImage( uint32_t index = 0 ) const = 0;
        virtual std::shared_ptr<Image2D> GetDepthAttachmentImage() const                     = 0;*/

        static std::shared_ptr<Framebuffer> Create( const FramebufferSpecification& spec );
    };
} // namespace Desert::Graphic