#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUniformBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.h>

#include <glm/glm.hpp>

namespace Desert::Graphic
{

    static struct ubo
    {
        glm::mat4 proj;
        glm::mat4 view;
    };

    static std::shared_ptr<API::Vulkan::VulkanUniformBuffer> s_Uniformbuffer;

    void SceneRenderer::UpdateDescriptorSets( const std::shared_ptr<Pipeline>& pipeline )
    {
        auto vkImage = std::static_pointer_cast<Graphic::API::Vulkan::VulkanImage2D>(
             m_SceneInfo.ActiveScene->GetEnvironment() );

        auto info = vkImage->GetVulkanImageInfo();

        VkDescriptorImageInfo imageInfo = {};
        imageInfo.sampler               = info.Sampler;
        imageInfo.imageView             = info.ImageView;
        imageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        VkDevice   device = API::Vulkan::VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        const auto shader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( pipeline->GetSpecification().Shader );

        VkWriteDescriptorSet writeDescriptorSet =
             shader->GetWriteDescriptorSet( API::Vulkan::WriteDescriptorType::Uniform, 0, 0, frameIndex );

        const auto pBufferInfo         = s_Uniformbuffer->GetDescriptorBufferInfo();
        writeDescriptorSet.pBufferInfo = &pBufferInfo;

        VkWriteDescriptorSet writeDescriptorSetImage = {};
        writeDescriptorSetImage.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSetImage.dstSet =
             shader->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );
        writeDescriptorSetImage.dstBinding      = 1;
        writeDescriptorSetImage.dstArrayElement = 0;
        writeDescriptorSetImage.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSetImage.descriptorCount = 1;
        writeDescriptorSetImage.pImageInfo      = &imageInfo;

        vkUpdateDescriptorSets( device, 1, &writeDescriptorSet, 0, nullptr );
        vkUpdateDescriptorSets( device, 1, &writeDescriptorSetImage, 0, nullptr );

        auto vp = m_SceneInfo.ActiveCamera->GetProjectionMatrix() * m_SceneInfo.ActiveCamera->GetViewMatrix();

        ubo ubob{ m_SceneInfo.ActiveCamera->GetProjectionMatrix(), m_SceneInfo.ActiveCamera->GetViewMatrix() };
        s_Uniformbuffer->RT_SetData( &ubob, sizeof( ubo ) );
    }

    void SceneRenderer::UpdateDescriptorSets2( void* dst, void* imageview, void* sampler )
    {

        VkDescriptorImageInfo imageInfo = {};
        imageInfo.sampler               = (VkSampler)sampler;
        imageInfo.imageView             = (VkImageView)imageview;
        imageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        VkDevice device = API::Vulkan::VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        VkWriteDescriptorSet writeDescriptorSetImage = {};
        writeDescriptorSetImage.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSetImage.dstSet               = (VkDescriptorSet)dst;
        writeDescriptorSetImage.dstBinding           = 2;
        writeDescriptorSetImage.dstArrayElement      = 0;
        writeDescriptorSetImage.descriptorType       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSetImage.descriptorCount      = 1;
        writeDescriptorSetImage.pImageInfo           = &imageInfo;

        vkUpdateDescriptorSets( device, 1, &writeDescriptorSetImage, 0, nullptr );
    }

    void SceneRenderer::Init()
    {
        REGISTER_EVENT( this, OnEvent );

        uint32_t width  = EngineContext::GetInstance().GetCurrentWindowWidth();
        uint32_t height = EngineContext::GetInstance().GetCurrentWindowHeight();

        {
            RenderPassSpecification renderPassSpecSkybox;
            renderPassSpecSkybox.DebugName = "Skybox";

            FramebufferSpecification TESTFramebufferFramebufferSpec;
            TESTFramebufferFramebufferSpec.DebugName = "Skybox";
            TESTFramebufferFramebufferSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA8F );
            m_TESTFramebufferSkybox = Graphic::Framebuffer::Create( TESTFramebufferFramebufferSpec );

            m_TESTFramebufferSkybox->Resize( width, height );

            m_TESTShaderSkybox = Graphic::Shader::Create( "skybox.glsl" );
            Graphic::PipelineSpecification spec;

            renderPassSpecSkybox.TargetFramebuffer = m_TESTFramebufferSkybox;
            m_TESTRenderPassSkybox                 = Graphic::RenderPass::Create( renderPassSpecSkybox );

            spec.Layout = { { Graphic::ShaderDataType::Float3, "a_Position" }};

            spec.DebugName       = "skybox";
            spec.Framebuffer     = m_TESTFramebufferSkybox;
            spec.Shader          = m_TESTShaderSkybox;
            m_TESTPipelineSkybox = Graphic::Pipeline::Create( spec );
            m_TESTPipelineSkybox->Invalidate();
        }

        // Composite
        {
            RenderPassSpecification renderPassSpec;
            renderPassSpec.DebugName = "SceneComposite";

            FramebufferSpecification framebufferSpec;
            framebufferSpec.DebugName = "SceneComposite";
            framebufferSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::BGRA8F );
            m_FramebufferComposite = Graphic::Framebuffer::Create( framebufferSpec );

            m_FramebufferComposite->Resize( width, height );

            m_ShaderComposite = Graphic::Shader::Create( "SceneComposite.glsl" );
            Graphic::PipelineSpecification spec;

            renderPassSpec.TargetFramebuffer = m_FramebufferComposite;
            m_RenderPassComposite            = Graphic::RenderPass::Create( renderPassSpec );

            spec.Layout = { { Graphic::ShaderDataType::Float3, "a_Position" } };

            spec.DebugName      = "SceneComposite";
            spec.Framebuffer    = m_FramebufferComposite;
            spec.Shader         = m_ShaderComposite;
            m_PipelineComposite = Graphic::Pipeline::Create( spec );
            m_PipelineComposite->Invalidate();
        }

        s_Uniformbuffer = std::make_shared<API::Vulkan::VulkanUniformBuffer>( sizeof( ubo ), 0 );
    }

    void SceneRenderer::BeginScene( const std::shared_ptr<Core::Scene>& scene, const Core::Camera& camera )
    {
        m_SceneInfo.ActiveScene  = scene;
        m_SceneInfo.ActiveCamera = const_cast<Core::Camera*>( &camera );

        auto& renderer = Renderer::GetInstance();

        renderer.BeginFrame();

        //  renderer.RenderImGui();
    }

    void SceneRenderer::EndScene()
    {
        auto& renderer = Renderer::GetInstance();

        GeometryRenderPass();
        CompositeRenderPass();

        renderer.EndFrame();
    }

    void SceneRenderer::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowResize>( [this]( Common::EventWindowResize& e )
                                                        { return this->OnWindowResize( e ); } );
    }

    bool SceneRenderer::OnWindowResize( Common::EventWindowResize& e )
    {
        auto&                                              renderer = Renderer::GetInstance();
        std::vector<std::shared_ptr<Graphic::Framebuffer>> framebuffers;
        framebuffers.push_back( m_TESTFramebufferSkybox );
        renderer.ResizeWindowEvent( e.width, e.height, framebuffers );
        return false;
    }

    void SceneRenderer::CompositeRenderPass()
    {

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        auto& renderer = Renderer::GetInstance();

        renderer.BeginSwapChainRenderPass( );
        /*a temporary solution until we add a material system.*/

        const auto imageInfo =
             sp_cast<API::Vulkan::VulkanImage2D>(
                  m_TESTPipelineSkybox->GetSpecification().Framebuffer->GetColorAttachmentImage() )
                  ->GetVulkanImageInfo();

        const auto dstSet = sp_cast<API::Vulkan::VulkanShader>( m_PipelineComposite->GetSpecification().Shader )
                                 ->GetVulkanDescriptorSetInfo()
                                 .DescriptorSets.at( frameIndex )
                                 .at( 0 );

        UpdateDescriptorSets2(dstSet, imageInfo.ImageView, imageInfo.Sampler);
        renderer.SubmitFullscreenQuad( m_PipelineComposite );
        renderer.EndRenderPass();
    }

    void SceneRenderer::GeometryRenderPass()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.BeginRenderPass( m_TESTRenderPassSkybox );
        /*a temporary solution until we add a material system.*/
        UpdateDescriptorSets( m_TESTPipelineSkybox );
        renderer.SubmitFullscreenQuad( m_TESTPipelineSkybox );
        renderer.EndRenderPass();
    }

} // namespace Desert::Graphic