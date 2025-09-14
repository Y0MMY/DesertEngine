#pragma once

#include <Engine/Graphic/RendererContext.hpp>
#include <Common/Core/Memory/CommandBuffer.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Geometry/Mesh.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>

namespace Desert::Graphic
{
    struct PBRTextures;
    class RendererAPI;

    class Renderer : public Common::Singleton<Renderer>
    {
    public:
        Common::BoolResult Init();
        void               Shutdown();

        [[nodiscard]] Common::BoolResult BeginFrame();
        [[nodiscard]] Common::BoolResult EndFrame();
        void BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass, bool clearFrame = false );
        void BeginSwapChainRenderPass();
        void EndRenderPass();
        void RenderMesh( const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Mesh>& mesh,
                         const std::shared_ptr<MaterialExecutor>& material );

        void SubmitFullscreenQuad( const std::shared_ptr<Pipeline>&         pipeline,
                                   const std::shared_ptr<MaterialExecutor>& material );

        void PrepareNextFrame();
        void PresentFinalImage();

        void ResizeWindowEvent( uint32_t width, uint32_t height );

        RendererAPI* GetRendererAPI() const;

        const std::shared_ptr<Graphic::Texture2D> GetBRDFTexture() const;

        std::shared_ptr<Framebuffer> GetCompositeFramebuffer();
        uint32_t                     GetCurrentFrameIndex();

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
        std::shared_ptr<Texture2D> m_BRDFTexture;
    };
} // namespace Desert::Graphic