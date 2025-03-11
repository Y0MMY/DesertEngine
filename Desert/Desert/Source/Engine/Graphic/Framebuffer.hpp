#pragma once

#include <Engine/Core/Formats/ImageFormat.hpp>
#include <Engine/Graphic/RendererTypes.hpp>
#include <Engine/Graphic/Image.hpp>

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
        std::string DebugName;
        bool NoResizeble = false;
    };

    class Framebuffer
    {
    public:
        virtual ~Framebuffer() = default;

        virtual const FramebufferSpecification GetSpecification() const                 = 0;
        virtual void                           Use( BindUsage = BindUsage::Bind ) const = 0;

        virtual Common::BoolResult Resize( uint32_t width, uint32_t height, bool forceRecreate = false ) = 0;

        virtual uint32_t GetFramebufferWidth() const  = 0;
        virtual uint32_t GetFramebufferHeight() const = 0;

        virtual std::shared_ptr<Image2D> GetColorAttachmentImage( uint32_t index = 0 ) const = 0;
        virtual std::shared_ptr<Image2D> GetDepthAttachmentImage() const                     = 0;

        static std::shared_ptr<Framebuffer> Create( const FramebufferSpecification& spec );
    };

    class FramebufferLibrary final
    {
    public:
        static const auto& GetFramebuffers()
        {
            return s_Framebuffers;
        }

    private:
        static inline std::vector<std::shared_ptr<Graphic::Framebuffer>> s_Framebuffers;

        friend class Framebuffer;
    };
} // namespace Desert::Graphic