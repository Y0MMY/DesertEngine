#pragma once

#include <Engine/Graphic/Framebuffer.hpp>

namespace Desert::Graphic::System
{
    class RenderSystem
    {
    public:
        explicit RenderSystem( const std::shared_ptr<Framebuffer>& compositeFramebuffer )
             : m_CompositeFramebuffer( compositeFramebuffer )
        {
        }
        virtual ~RenderSystem() = default;

        virtual Common::BoolResult Initialize( const uint32_t width, const uint32_t height ) = 0;
        virtual void               Shutdown()                                                = 0;
        virtual void               ProcessSystem()                                           = 0;

        virtual std::shared_ptr<Framebuffer> GetSystemFramebuffer() const final
        {
            return m_Framebuffer;
        }

    protected:
        std::weak_ptr<Framebuffer>   m_CompositeFramebuffer;
        std::shared_ptr<Framebuffer> m_Framebuffer;
    };
} // namespace Desert::Graphic::System