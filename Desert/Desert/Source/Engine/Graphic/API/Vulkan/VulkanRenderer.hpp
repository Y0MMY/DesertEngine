#pragma once

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanRendererAPI : public RendererAPI
    {
    public:
        virtual void Init() override;
        virtual void Shutdown() override;

        [[nodiscard]] virtual Common::BoolResult BeginFrame() override;
        [[nodiscard]] virtual Common::BoolResult EndFrame() override;
        [[nodiscard]] virtual Common::BoolResult PrepareNextFrame() override;
        [[nodiscard]] virtual Common::BoolResult PresentFinalImage() override;
        [[nodiscard]] virtual Common::BoolResult
                                   BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass ) override;
        virtual Common::BoolResult BeginSwapChainRenderPass() override;
        [[nodiscard]] virtual Common::BoolResult EndRenderPass() override;
        virtual void RenderMesh( const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Mesh>& mesh,
                                 const std::shared_ptr<MaterialExecutor>& material ) override;

        virtual void SubmitFullscreenQuad( const std::shared_ptr<Pipeline>&         pipeline,
                                           const std::shared_ptr<MaterialExecutor>& material ) override;

        virtual void ResizeWindowEvent( uint32_t width, uint32_t height ) override;

        virtual std::shared_ptr<Framebuffer> GetCompositeFramebuffer() const override;

        VkCommandBuffer GetCurrentCmdBuffer() const;

    private:
        void SetViewportAndScissor();

    private:
        VkCommandBuffer m_CurrentCommandBuffer = nullptr;

        std::weak_ptr<Framebuffer> m_CompositeFramebuffer;
    };

} // namespace Desert::Graphic::API::Vulkan