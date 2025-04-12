#pragma once

#include <Engine/Graphic/RendererContext.hpp>
#include <Common/Core/Memory/CommandBuffer.hpp>

#include <Engine/Graphic/Material.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Mesh.hpp>

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

        [[nodiscard]] Common::BoolResult BeginFrame();
        [[nodiscard]] Common::BoolResult EndFrame();
        void                             BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass );
        void                             BeginSwapChainRenderPass();
        void                             EndRenderPass();
        void RenderMesh( const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Mesh>& mesh,
                         const glm::mat4& mvp /*TEMP*/ );

        void SubmitFullscreenQuad( const std::shared_ptr<Pipeline>& pipeline,
                                   const std::shared_ptr<Material>& material );

        void PresentFinalImage();

        void ResizeWindowEvent( uint32_t width, uint32_t height,
                                const std::vector<std::shared_ptr<Framebuffer>>& framebuffers );

        std::shared_ptr<Framebuffer> GetCompositeFramebuffer();

        std::shared_ptr<Image2D> CreateEnvironmentMap( const Common::Filepath& filepath );

        uint32_t GetCurrentFrameIndex();

        template <typename FuncT>
        static void SubmitCommand( FuncT&& func )
        {
            Common::Memory::SubmitCommand( GetRenderCommandQueue(), std::forward<FuncT>( func ) );
        }

    private:
        [[nodiscard]] Common::BoolResult InitGraphicAPI();

    private:
        static Common::Memory::CommandBuffer& GetRenderCommandQueue();

    private:
        std::shared_ptr<RendererContext> m_RendererContext;
    };
} // namespace Desert::Graphic