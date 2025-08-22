#pragma once

#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/RenderGraph.hpp>

#include <Common/Core/CommonContext.hpp>

namespace Desert::Graphic::System
{
    class RenderSystem
    {
    public:
        explicit RenderSystem( const std::shared_ptr<Framebuffer>& compositeFramebuffer,
                               const std::shared_ptr<RenderGraph>& renderGraph )
             : m_CompositeFramebuffer( compositeFramebuffer ), m_RenderGraph( renderGraph )
        {
            m_Width  = Common::CommonContext::GetInstance().GetCurrentWindowWidth();
            m_Height = Common::CommonContext::GetInstance().GetCurrentWindowHeight();
        }
        virtual ~RenderSystem() = default;

        virtual Common::BoolResult Initialize() = 0;
        virtual void               Shutdown()   = 0;

        virtual std::shared_ptr<Framebuffer> GetSystemFramebuffer() const final
        {
            return m_Framebuffer;
        }

    protected:
        uint32_t m_Width;
        uint32_t m_Height;

    protected:
        std::weak_ptr<Framebuffer> m_CompositeFramebuffer;
        std::weak_ptr<RenderGraph> m_RenderGraph;

        std::shared_ptr<Framebuffer> m_Framebuffer;
    };
} // namespace Desert::Graphic::System