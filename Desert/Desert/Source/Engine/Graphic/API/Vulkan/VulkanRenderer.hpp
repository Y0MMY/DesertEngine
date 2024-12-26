#pragma once

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanRendererAPI : public RendererAPI
    {
    public:
        virtual void Init() override;

        virtual void BeginFrame() override;
        virtual void PresentFinalImage() override;
        virtual void EndFrame() override;
    private:
        VkCommandBuffer m_CurrentCommandBuffer = nullptr;

        std::shared_ptr<Framebuffer> m_framebuffer; //temp
        std::shared_ptr<Shader> m_Shader; //temp
        std::shared_ptr<Pipeline> m_Pipeline; //temp
        std::shared_ptr<VertexBuffer> m_VertexBuffer; //temp
    };

} // namespace Desert::Graphic::API::Vulkan