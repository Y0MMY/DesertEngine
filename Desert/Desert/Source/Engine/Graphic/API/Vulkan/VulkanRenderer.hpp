#pragma once

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>
#include <Engine/Graphic/UniformBuffer.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanTexture.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanRendererAPI : public RendererAPI
    {
    public:
        virtual void Init() override;

        virtual [[nodiscard]] Common::BoolResult BeginFrame() override;
        virtual [[nodiscard]] Common::BoolResult EndFrame() override;
        virtual [[nodiscard]] Common::BoolResult PresentFinalImage() override;
        virtual [[nodiscard]] Common::BoolResult
        BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass ) override;
        virtual [[nodiscard]] Common::BoolResult EndRenderPass() override;

        virtual void TEST_DrawTriangle( const std::shared_ptr<VertexBuffer>& vertexBuffer,
                                        const std::shared_ptr<IndexBuffer>&  indexBuffer,

                                        const std::shared_ptr<Pipeline>& pipeline ) override; // TEMP

        virtual void ResizeWindowEvent( uint32_t width, uint32_t height,
                                        const std::vector<std::shared_ptr<Framebuffer>>& framebuffers ) override;

        virtual std::shared_ptr<Framebuffer> GetCompositeFramebuffer() const override;

    private:
        void UpdateDescriptorSets( const std::shared_ptr<Pipeline>& pipeline );

    private:
        VkCommandBuffer m_CurrentCommandBuffer = nullptr;

        std::shared_ptr<UniformBuffer> m_UniformBuffer;
        std::shared_ptr<Framebuffer>   m_CompositeFramebuffer;
        std::shared_ptr<TextureCube>   m_texture;
    };

} // namespace Desert::Graphic::API::Vulkan