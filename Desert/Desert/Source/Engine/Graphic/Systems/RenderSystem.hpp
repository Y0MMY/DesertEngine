#pragma once

#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/RenderGraph.hpp>

namespace Desert::Graphic::System
{
    class RenderSystem
    {
    public:
        explicit RenderSystem( const std::shared_ptr<Framebuffer>& compositeFramebuffer,
                               const std::shared_ptr<RenderGraph>& renderGraph )
             : m_CompositeFramebuffer( compositeFramebuffer ), m_RenderGraph( renderGraph )
        {
        }
        virtual ~RenderSystem() = default;

        virtual Common::BoolResult Initialize( const uint32_t width, const uint32_t height ) = 0;
        virtual void               Shutdown()                                                = 0;

        virtual std::shared_ptr<Framebuffer> GetSystemFramebuffer() const final
        {
            return m_Framebuffer;
        }

    protected:
        std::weak_ptr<Framebuffer> m_CompositeFramebuffer;
        std::weak_ptr<RenderGraph> m_RenderGraph;

        std::shared_ptr<Framebuffer> m_Framebuffer;
    };
} // namespace Desert::Graphic::System