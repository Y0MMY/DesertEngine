#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Core/Window.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Graphic/Image.hpp>

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
        
        virtual void RenderMesh( const GraphicsPipeline* pipeline, const Mesh* mesh, const glm::mat4 transform,
                                 const MaterialExecutor* materialExecutor )                            = 0;
        
        virtual void SubmitFullscreenQuad( const GraphicsPipeline* pipeline,
                                           const MaterialExecutor* materialExecutor )                  = 0;

        // Vertexless line draw (Lines-topology pipeline pulls vertices from a storage buffer by index).
        virtual void SubmitLines( const GraphicsPipeline* pipeline, uint32_t vertexCount, float lineWidth,
                                  const MaterialExecutor* materialExecutor )                            = 0;

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
