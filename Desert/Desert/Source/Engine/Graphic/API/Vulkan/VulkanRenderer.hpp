#pragma once

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanRendererAPI : public RendererAPI
    {
    public:
        virtual void Init() override;

        virtual Common::BoolResult BeginFrame() override;
        virtual Common::BoolResult EndFrame() override;
        virtual Common::BoolResult PresentFinalImage() override;
        virtual Common::BoolResult BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass ) override;
        virtual Common::BoolResult EndRenderPass() override;

        virtual void TEST_DrawTriangle( const std::shared_ptr<VertexBuffer>& vertexBuffer,
                                        const std::shared_ptr<Pipeline>&     pipeline ) override; // TEMP

    private:
        VkCommandBuffer m_CurrentCommandBuffer = nullptr;
    };

} // namespace Desert::Graphic::API::Vulkan