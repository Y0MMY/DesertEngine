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

        /**
         * @brief Dispatches a compute shader.
         */
        virtual void DispatchCompute( const ComputePipeline* pipeline,
                                      uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ,
                                      const MaterialExecutor* materialExecutor = nullptr ) = 0;

        /**
         * @brief One-shot compute dispatch outside of a render frame (e.g. environment map generation).
         *        Allocates a dedicated command buffer, transitions images, dispatches, then flushes synchronously.
         */
        virtual void ImmediateComputeDispatch( const ComputePipeline* pipeline,
                                               Image2D*   inputImage,
                                               ImageCube* outputImage,
                                               uint32_t groupCountX, uint32_t groupCountY,
                                               uint32_t groupCountZ ) = 0;

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
