#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Core/Window.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::ShaderResources
{
    class StorageBuffer;
}

namespace Desert::Graphic
{
    enum class RendererAPIType : uint8_t
    {
        None   = 0,
        Vulkan = 1,
    };

    class RendererAPI
    {
    public:
        explicit RendererAPI( const std::shared_ptr<Window>& window ) : m_Window( window )
        {
        }
        virtual ~RendererAPI() = default;

    public:
        virtual void Init()     = 0;
        virtual void Shutdown() = 0;

        virtual Common::BoolResultStr BeginFrame()                                                     = 0;
        virtual Common::BoolResultStr EndFrame()                                                       = 0;
        virtual Common::BoolResultStr PrepareNextFrame()                                               = 0;
        virtual Common::BoolResultStr PresentFinalImage()                                              = 0;
        virtual Common::BoolResultStr BeginRenderPass( const RenderPass* renderPass, bool clearFrame ) = 0;
        virtual Common::BoolResultStr BeginSwapChainRenderPass()                                       = 0;
        virtual Common::BoolResultStr EndRenderPass()                                                  = 0;

        // Named command-buffer region for graphics debuggers (RenderDoc shows these as a pass
        // tree). Default no-op so non-debug backends don't have to care.
        virtual void BeginDebugLabel( const char* name )
        {
        }
        virtual void EndDebugLabel()
        {
        }
        
        // @p instanceCount > 1 issues a hardware-instanced draw (the instanced pipeline's vertex shader
        // reads the per-instance model matrix from an InstanceTransforms SSBO by gl_InstanceIndex).
        // @p firstInstance offsets gl_InstanceIndex (== firstInstance + 0..instanceCount-1), so several
        // mesh sub-groups can share ONE packed InstanceTransforms buffer (each draw reads its own slice).
        virtual void RenderMesh( const GraphicsPipeline* pipeline, const Mesh* mesh, const glm::mat4 transform,
                                 const MaterialExecutor* materialExecutor, uint32_t instanceCount = 1,
                                 uint32_t firstInstance = 0, uint64_t hiddenSubmeshMask = 0,
                                 uint32_t lodLevel = 0 ) = 0;
        
        virtual void SubmitFullscreenQuad( const GraphicsPipeline* pipeline,
                                           const MaterialExecutor* materialExecutor )                  = 0;

        // Indexed draw from caller-supplied vertex + index buffers. The 2D/UI batcher fills a dynamic
        // VB+IB each frame (one buffer, many quads) and issues one SubmitIndexed per state batch;
        // @p materialExecutor supplies the batch's texture and push constants (the ortho projection).
        virtual void SubmitIndexed( const GraphicsPipeline* pipeline, VertexBuffer* vertexBuffer,
                                    IndexBuffer* indexBuffer, uint32_t indexCount, uint32_t firstIndex,
                                    const MaterialExecutor* materialExecutor ) = 0;

        // Vertexless line draw (Lines-topology pipeline pulls vertices from a storage buffer by index).
        virtual void SubmitLines( const GraphicsPipeline* pipeline, uint32_t vertexCount, float lineWidth,
                                  const MaterialExecutor* materialExecutor )                            = 0;

        // Vertexless draw of @p vertexCount vertices (no vertex/index buffer bound). The vertex shader
        // synthesizes geometry from gl_VertexIndex. Used by the GPU terrain (patch-list tessellation).
        // @p instanceCount > 1 issues an instanced draw (gl_InstanceIndex per instance) — GPU-driven
        // foliage (grass) derives each blade's transform from gl_InstanceIndex without an instance buffer.
        virtual void SubmitVertices( const GraphicsPipeline* pipeline, uint32_t vertexCount,
                                     const MaterialExecutor* materialExecutor,
                                     uint32_t                instanceCount = 1 )                        = 0;

        // GPU-driven instanced draw whose instanceCount is produced on the GPU: @p argsBuffer holds a
        // VkDrawIndirectCommand (vertexCount, instanceCount, firstVertex, firstInstance) written by a
        // prior compute cull pass. Used by grass: the cull compute compacts visible clumps and writes
        // the count, so no CPU readback / no per-instance VS work for culled clumps.
        virtual void SubmitVerticesIndirect( const GraphicsPipeline*         pipeline,
                                             ShaderResources::StorageBuffer* argsBuffer,
                                             const MaterialExecutor*         materialExecutor ) = 0;

        // Like DispatchComputeInFrame but the compute writes are made visible to the VERTEX stage
        // (storage read) and to the DRAW_INDIRECT stage (indirect command read) — for GPU cull passes
        // that feed an indirect instanced draw.
        virtual void DispatchComputeCull( const ComputePipeline* pipeline, uint32_t groupCountX,
                                          uint32_t groupCountY, uint32_t groupCountZ ) = 0;

        /**
         * @brief Records a compute dispatch into the current frame command buffer (outside any render
         *        pass), then inserts a compute-write -> shader-read barrier so the next dispatch or a
         *        later fragment sample sees the result. The pipeline's bound inputs/outputs/push-constants
         *        are consumed (see ComputePipeline::SetInput/SetOutput/SetPushConstants). The caller owns
         *        image layout transitions (see ComputeImageBeginWrite / ComputeImageEndWrite).
         */
        virtual void DispatchComputeInFrame( const ComputePipeline* pipeline, uint32_t groupCountX,
                                             uint32_t groupCountY, uint32_t groupCountZ ) = 0;

        // Transition a storage image to GENERAL for compute writes in the current frame command buffer,
        // making prior graphics (color/shader) writes visible to compute. Pair with ComputeImageEndWrite.
        virtual void ComputeImageBeginWrite( Image2D* image ) = 0;
        // Transition the storage image back to SHADER_READ_ONLY for later sampling (e.g. tonemap),
        // making the compute writes visible to the fragment stage.
        virtual void ComputeImageEndWrite( Image2D* image ) = 0;

        // Copy the DEPTH aspect of @p src into @p dst (same format + extent). Must be called OUTSIDE any
        // render pass. Used by the Deferred path to resolve the G-buffer depth into the scene target depth
        // so depth-tested overlays (grid, colliders) occlude against the static geometry that the deferred
        // composite never wrote to the target depth.
        virtual void CopyDepthImage( Image2D* src, Image2D* dst ) = 0;

        virtual void                         ResizeWindowEvent( uint32_t width, uint32_t height ) = 0;
        virtual void                         WaitDeviceIdle()                                     = 0;
        virtual std::shared_ptr<Framebuffer> GetCompositeFramebuffer() const                      = 0;

    public:
        static const RendererAPIType GetAPIType()
        {
            return s_RenderingAPI;
        }

    protected:
        std::weak_ptr<Window>         m_Window;
        static inline RendererAPIType s_RenderingAPI = RendererAPIType::Vulkan;
    };

} // namespace Desert::Graphic
