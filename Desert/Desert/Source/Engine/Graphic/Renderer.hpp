#pragma once

#include <Engine/Graphic/RendererContext.hpp>
#include <Common/Core/Memory/CommandBuffer.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>

namespace Desert::Graphic
{
    struct PBRTextures;
    class RendererAPI;

    class Renderer : public Common::Singleton<Renderer>
    {
    public:
        Common::BoolResultStr Init();
        void                  Shutdown();

        [[nodiscard]] Common::BoolResultStr BeginFrame();
        [[nodiscard]] Common::BoolResultStr EndFrame();
        void BeginRenderPass( const RenderPass* renderPass, bool clearFrame = false );
        void BeginSwapChainRenderPass();
        void EndRenderPass();
        void RenderMesh( const GraphicsPipeline* pipeline, const Mesh* mesh, const glm::mat4 transform,
                         const MaterialExecutor* materialExecutor );

        void SubmitFullscreenQuad( const GraphicsPipeline* pipeline, const MaterialExecutor* materialExecutor );

        void DispatchCompute( const ComputePipeline* pipeline, uint32_t groupCountX, uint32_t groupCountY,
                              uint32_t groupCountZ, const MaterialExecutor* materialExecutor = nullptr );

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
        [[nodiscard]] Common::BoolResultStr InitGraphicAPI();

    private:
        static Common::Memory::CommandBuffer& GetRenderCommandQueue();

    private:
        std::shared_ptr<Texture2D> m_BRDFTexture;
    };
} // namespace Desert::Graphic