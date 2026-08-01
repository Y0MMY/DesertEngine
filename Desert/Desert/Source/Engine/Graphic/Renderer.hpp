#pragma once

#include <Engine/Graphic/RendererContext.hpp>
#include <Common/Core/Memory/CommandBuffer.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::ShaderResources
{
    class StorageBuffer;
}

namespace Desert::Graphic
{
    class RendererAPI;

    class Renderer : public Common::Singleton<Renderer>
    {
    public:
        Common::BoolResultStr Init();
        void                  Shutdown();

        [[nodiscard]] Common::BoolResultStr BeginFrame();
        [[nodiscard]] Common::BoolResultStr EndFrame();
        void BeginRenderPass( const RenderPass* renderPass, bool clearFrame = true );
        void BeginSwapChainRenderPass();
        void EndRenderPass();

        // Named region in the current command buffer (RenderDoc pass tree). Pair Begin/End.
        void BeginDebugLabel( const char* name );
        void EndDebugLabel();
        void RenderMesh( const GraphicsPipeline* pipeline, const Mesh* mesh, const glm::mat4 transform,
                         const MaterialExecutor* materialExecutor, uint32_t instanceCount = 1,
                         uint32_t firstInstance = 0, uint64_t hiddenSubmeshMask = 0, uint32_t lodLevel = 0 );

        void SubmitFullscreenQuad( const GraphicsPipeline* pipeline, const MaterialExecutor* materialExecutor );

        // Indexed draw from a caller-supplied dynamic VB+IB (the 2D/UI batcher). One call per state batch.
        void SubmitIndexed( const GraphicsPipeline* pipeline, VertexBuffer* vertexBuffer, IndexBuffer* indexBuffer,
                            uint32_t indexCount, uint32_t firstIndex, const MaterialExecutor* materialExecutor );

        // Vertexless line draw: the pipeline (Lines topology) pulls vertices from a storage buffer by index.
        void SubmitLines( const GraphicsPipeline* pipeline, uint32_t vertexCount, float lineWidth,
                          const MaterialExecutor* materialExecutor );

        // Vertexless draw: the vertex shader synthesizes geometry from gl_VertexIndex (GPU terrain patches).
        // instanceCount > 1 -> instanced draw (gl_InstanceIndex), used by GPU-driven grass foliage.
        void SubmitVertices( const GraphicsPipeline* pipeline, uint32_t vertexCount,
                             const MaterialExecutor* materialExecutor, uint32_t instanceCount = 1 );

        // GPU-driven instanced draw whose instanceCount comes from @p argsBuffer (a VkDrawIndirectCommand
        // written by a compute cull pass). Used by GPU-culled grass.
        void SubmitVerticesIndirect( const GraphicsPipeline* pipeline, ShaderResources::StorageBuffer* argsBuffer,
                                     const MaterialExecutor* materialExecutor );

        // In-frame compute dispatch (records into the frame command buffer outside any render pass,
        // inserts a trailing compute->shader barrier). The compute mip-chain bloom is built on this.
        void DispatchComputeInFrame( const ComputePipeline* pipeline, uint32_t groupCountX,
                                     uint32_t groupCountY, uint32_t groupCountZ );

        // Compute dispatch whose writes are made visible to the VERTEX + DRAW_INDIRECT stages (GPU cull
        // feeding an indirect instanced draw).
        void DispatchComputeCull( const ComputePipeline* pipeline, uint32_t groupCountX, uint32_t groupCountY,
                                  uint32_t groupCountZ );

        // Layout helpers for compute storage targets used in the frame command buffer (see RendererAPI).
        void ComputeImageBeginWrite( Image2D* image );
        void ComputeImageEndWrite( Image2D* image );

        // Copy the depth aspect of @p src into @p dst (Deferred: G-buffer depth -> scene target depth so
        // depth-tested overlays occlude against static geometry). Call outside a render pass.
        void CopyDepthImage( Image2D* src, Image2D* dst );

        // Set the scissor rect (framebuffer px, top-left origin). Used by the 2D batcher for UI clipping.
        void SetScissor( int32_t x, int32_t y, uint32_t width, uint32_t height );

        void PrepareNextFrame();
        void PresentFinalImage();

        void ResizeWindowEvent( uint32_t width, uint32_t height );
        void WaitDeviceIdle();

        // Recreate every registered image's sampler from the current RenderConfig filter (live filter swap).
        void RecreateImageSamplers();

        RendererAPI* GetRendererAPI() const;

        const std::shared_ptr<Graphic::Texture2D>& GetBRDFTexture() const;

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