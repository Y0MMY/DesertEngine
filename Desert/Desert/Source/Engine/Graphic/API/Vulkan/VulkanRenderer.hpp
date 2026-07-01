#pragma once

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanRendererAPI : public RendererAPI
    {
    public:
        explicit VulkanRendererAPI( const std::shared_ptr<Window>& window ) : RendererAPI( window )
        {
        }
        virtual void Init() override;
        virtual void Shutdown() override;

        [[nodiscard]] virtual Common::BoolResultStr BeginFrame() override;
        [[nodiscard]] virtual Common::BoolResultStr EndFrame() override;
        [[nodiscard]] virtual Common::BoolResultStr PrepareNextFrame() override;
        [[nodiscard]] virtual Common::BoolResultStr PresentFinalImage() override;
        [[nodiscard]] virtual Common::BoolResultStr BeginRenderPass( const RenderPass* renderPass,
                                                                     bool              clearFrame ) override;
        virtual Common::BoolResultStr               BeginSwapChainRenderPass() override;
        [[nodiscard]] virtual Common::BoolResultStr EndRenderPass() override;
        
        virtual void RenderMesh( const GraphicsPipeline* pipeline, const Mesh* mesh, const glm::mat4 transform,
                                 const MaterialExecutor* materialExecutor, uint32_t instanceCount = 1,
                                 uint32_t firstInstance = 0, uint64_t hiddenSubmeshMask = 0 ) override;

        virtual void SubmitFullscreenQuad( const GraphicsPipeline*         pipeline,
                                           const MaterialExecutor* materialExecutor ) override;

        virtual void SubmitLines( const GraphicsPipeline* pipeline, uint32_t vertexCount, float lineWidth,
                                  const MaterialExecutor* materialExecutor ) override;

        virtual void SubmitVertices( const GraphicsPipeline* pipeline, uint32_t vertexCount,
                                     const MaterialExecutor* materialExecutor,
                                     uint32_t                instanceCount = 1 ) override;

        virtual void SubmitVerticesIndirect( const GraphicsPipeline*         pipeline,
                                             ShaderResources::StorageBuffer* argsBuffer,
                                             const MaterialExecutor*         materialExecutor ) override;

        virtual void DispatchComputeInFrame( const ComputePipeline* pipeline, uint32_t groupCountX,
                                             uint32_t groupCountY, uint32_t groupCountZ ) override;

        virtual void DispatchComputeCull( const ComputePipeline* pipeline, uint32_t groupCountX,
                                          uint32_t groupCountY, uint32_t groupCountZ ) override;

        virtual void ComputeImageBeginWrite( Image2D* image ) override;
        virtual void ComputeImageEndWrite( Image2D* image ) override;

        virtual void ResizeWindowEvent( uint32_t width, uint32_t height ) override;
        virtual void WaitDeviceIdle() override;

        virtual std::shared_ptr<Framebuffer> GetCompositeFramebuffer() const override;

        VkCommandBuffer GetCurrentCmdBuffer() const;

    private:
        void SetViewportAndScissor( const uint32_t width, const uint32_t height );
        void ClearAttachments( const std::vector<VkClearValue>&    clearValues,
                               const std::shared_ptr<Framebuffer>& framebuffer );

    private:
        VkCommandBuffer m_CurrentCommandBuffer = nullptr;

        std::weak_ptr<Framebuffer> m_CompositeFramebuffer;
    };

} // namespace Desert::Graphic::API::Vulkan
