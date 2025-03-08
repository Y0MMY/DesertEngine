#pragma once

#include <Engine/Graphic/RendererContext.hpp>
#include <Common/Core/Memory/CommandBuffer.hpp>

#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>
#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Texture.hpp>

namespace Desert::Graphic
{
    class Renderer : public Common::Singleton<Renderer>
    {
    public:
        Common::BoolResult Init();

        const auto& GetRendererContext() const
        {
            return m_RendererContext;
        }

        void BeginFrame();
        void EndFrame();
        void BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass );
        void EndRenderPass();
        void RenderImGui();

        void SubmitFullscreenQuad( const std::shared_ptr<Pipeline>& pipeline );

        void PresentFinalImage();

        void ResizeWindowEvent( uint32_t width, uint32_t height,
                                const std::vector<std::shared_ptr<Framebuffer>>& framebuffers );

        std::shared_ptr<Framebuffer> GetCompositeFramebuffer();

        uint32_t GetCurrentFrameIndex();

        template <typename FuncT>
        static void SubmitCommand( FuncT&& func )
        {
            Common::Memory::SubmitCommand( GetRenderCommandQueue(), std::forward<FuncT>( func ) );
        }

    private:
        void InitGraphicAPI();

    private:
        static Common::Memory::CommandBuffer& GetRenderCommandQueue();

    private:
        std::shared_ptr<RendererContext> m_RendererContext;
    };
} // namespace Desert::Graphic