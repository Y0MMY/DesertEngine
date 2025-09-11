#pragma once

#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/RenderGraph.hpp>

#include <Engine/Core/EngineContext.hpp>

namespace Desert::Graphic
{
    class SceneRenderer;
}

namespace Desert::Graphic::System
{
    class RenderSystem
    {
    public:
        explicit RenderSystem( SceneRenderer* sceneRenderer, const std::shared_ptr<Framebuffer>& targetFramebuffer,
                               const std::shared_ptr<RenderGraph>& renderGraph )
             : m_SceneRenderer( sceneRenderer ), m_TargetFramebuffer( targetFramebuffer ),
               m_RenderGraph( renderGraph )
        {
        }
        virtual ~RenderSystem() = default;

        virtual Common::BoolResult Initialize() = 0;
        virtual void               Shutdown()   = 0;

        virtual std::shared_ptr<Framebuffer> GetSystemFramebuffer() const final
        {
            return m_Framebuffer;
        }

    protected:
        SceneRenderer*             m_SceneRenderer;
        std::weak_ptr<Framebuffer> m_TargetFramebuffer;
        std::weak_ptr<RenderGraph> m_RenderGraph;

        std::shared_ptr<Framebuffer> m_Framebuffer;
    };
} // namespace Desert::Graphic::System