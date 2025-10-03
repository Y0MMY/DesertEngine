#pragma once

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <vulkan/vulkan.hpp>

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
        [[nodiscard]] virtual Common::BoolResultStr BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass,
                                                                  bool clearFrame ) override;
        virtual Common::BoolResultStr               BeginSwapChainRenderPass() override;
        [[nodiscard]] virtual Common::BoolResultStr EndRenderPass() override;
        virtual void RenderMesh( const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Mesh>& mesh,
                                 const std::shared_ptr<MaterialExecutor>& material ) override;

        virtual void SubmitFullscreenQuad( const std::shared_ptr<Pipeline>&         pipeline,
                                           const std::shared_ptr<MaterialExecutor>& material ) override;

        virtual void ResizeWindowEvent( uint32_t width, uint32_t height ) override;

        virtual std::shared_ptr<Framebuffer> GetCompositeFramebuffer() const override;

        VkCommandBuffer GetCurrentCmdBuffer() const;

    private:
        void SetViewportAndScissor( const uint32_t wdith, const uint32_t height );
        void ClearAttachments( const std::vector<VkClearValue>&    clearValues,
                               const std::shared_ptr<Framebuffer>& framebuffer );

    private:
        VkCommandBuffer m_CurrentCommandBuffer = nullptr;

        std::weak_ptr<Framebuffer> m_CompositeFramebuffer;
    };

} // namespace Desert::Graphic::API::Vulkan