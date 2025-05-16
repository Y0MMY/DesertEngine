#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanVertexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanIndexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Texture.hpp>

#include <Engine/Core/EngineContext.h>
#include "stb_image/stb_image_write.h"

#include <glm/glm.hpp>

namespace Desert::Graphic::API::Vulkan
{
    static constexpr uint32_t kEnvFaceMapSize    = 512;
    static constexpr uint32_t kIrradianceMapSize = 32;
    static constexpr uint32_t kBRDF_LUT_Size     = 256;
    static constexpr uint32_t kWorkGroups        = 32;

    struct SpecularFilterPushConstants
    {
        float    roughness;
        uint32_t mipLevel;
    };

    struct ComputeImageProcessingInfo
    {
        std::string                shaderName;
        uint32_t                   width;
        uint32_t                   height;
        Core::Formats::ImageFormat format = Core::Formats::ImageFormat::RGBA32F;
        Core::Formats::ImageUsage  usage  = Core::Formats::ImageUsage::Image2D;
        std::function<void( const std::shared_ptr<PipelineCompute>& )> pushConstantsCallback = nullptr;
    };

    namespace Utils
    {
        std::shared_ptr<Image2D> CreateProcessedImage( const std::shared_ptr<VulkanImage2D>& inputImage,
                                                       const ComputeImageProcessingInfo&     processingInfo )
        {
            static std::unordered_map<std::string, std::shared_ptr<Shader>> shaderCache;

            auto& shader = shaderCache[processingInfo.shaderName];
            if ( !shader )
            {
                shader = Shader::Create( processingInfo.shaderName );
            }

            Core::Formats::ImageSpecification outputImageInfo;
            outputImageInfo.Width  = processingInfo.width;
            outputImageInfo.Height = processingInfo.height;
            outputImageInfo.Mips   = 1;
            // Graphic::Utils::CalculateMipCount( processingInfo.width / 4, processingInfo.height / 3 );
            outputImageInfo.Format     = processingInfo.format;
            outputImageInfo.Properties = Core::Formats::Storage | Core::Formats::Sample;
            outputImageInfo.Usage      = processingInfo.usage;

            const std::shared_ptr<Image2D> outputImage       = Image2D::Create( outputImageInfo );
            const auto&                    outputImageVulkan = sp_cast<API::Vulkan::VulkanImage2D>( outputImage );
            outputImageVulkan->RT_Invalidate();

            const auto& shaderVulkan = sp_cast<API::Vulkan::VulkanShader>( shader );
            uint32_t    frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();

            std::array<VkDescriptorImageInfo, 2> imageInfo = {};
            imageInfo[0].imageView                         = outputImageVulkan->GetVulkanImageInfo().ImageView;
            imageInfo[0].imageLayout                       = VK_IMAGE_LAYOUT_GENERAL;

            imageInfo[1].imageView   = inputImage->GetVulkanImageInfo().ImageView;
            imageInfo[1].sampler     = inputImage->GetVulkanImageInfo().Sampler;
            imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            std::array<VkWriteDescriptorSet, 2> descriptorWrite = {};

            auto vulkanPipelineCompute = Graphic::PipelineCompute::Create( shader );
            vulkanPipelineCompute->Invalidate();

            // Output image (storage)
            descriptorWrite[0] = shaderVulkan->GetWriteDescriptorSet(
                 API::Vulkan::WriteDescriptorType::StorageImage, 1, 0, frameIndex );
            descriptorWrite[0].dstSet =
                 shaderVulkan->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );
            descriptorWrite[0].pImageInfo = &imageInfo[0];

            // Input image (sampler)
            descriptorWrite[1] = shaderVulkan->GetWriteDescriptorSet( API::Vulkan::WriteDescriptorType::Sampler2D,
                                                                      0, 0, frameIndex );
            descriptorWrite[1].dstSet =
                 shaderVulkan->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );
            descriptorWrite[1].pImageInfo = &imageInfo[1];

            VkDevice device = API::Vulkan::VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
            vkUpdateDescriptorSets( device, descriptorWrite.size(), descriptorWrite.data(), 0, nullptr );

            auto vulkanPipeline = sp_cast<API::Vulkan::VulkanPipelineCompute>( vulkanPipelineCompute );

            vulkanPipelineCompute->Begin();
            vulkanPipeline->BindDS( descriptorWrite[0].dstSet );

            if ( processingInfo.pushConstantsCallback )
            {
                processingInfo.pushConstantsCallback( vulkanPipelineCompute );
            }
            else
            {
                const uint32_t workGroupsX = processingInfo.width / kWorkGroups;
                const uint32_t workGroupsY = processingInfo.height / kWorkGroups;
                const uint32_t workGroupsZ = 6;

                vulkanPipelineCompute->Dispatch( workGroupsX, workGroupsY, workGroupsZ );
            }
            vulkanPipelineCompute->End();
            return outputImage;
        }

    } // namespace Utils

    struct VulkanRendererData
    {
        std::shared_ptr<VulkanVertexBuffer> QuadVertexBuffer;
        std::shared_ptr<VulkanIndexBuffer>  QuadIndexBuffer;
    };

    static VulkanRendererData* s_Data = nullptr;

    Common::BoolResult VulkanRendererAPI::BeginFrame()
    {
        if ( m_CurrentCommandBuffer != nullptr )
        {
            return Common::MakeError<bool>( "BeginFrame(): Error! Have you call EndFrame() ?" );
        }

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        m_CurrentCommandBuffer = VulkanRenderCommandBuffer::GetInstance().GetCommandBuffer();
        vkBeginCommandBuffer( m_CurrentCommandBuffer, &cmdBufferBeginInfo );

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::EndFrame()
    {
        if ( m_CurrentCommandBuffer == nullptr )
        {
            return Common::MakeError<bool>( "EndFrame(): Error! Have you call BeginFrame() ?" );
        }

        VulkanRenderCommandBuffer::GetInstance().ExecuteUserCommands();
        VkResult res = vkEndCommandBuffer( m_CurrentCommandBuffer );

        m_CurrentCommandBuffer = nullptr;

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::PresentFinalImage()
    {
        std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
             Renderer::GetInstance().GetRendererContext() )
             ->PresentFinalImage();

        return Common::MakeSuccess( true );
    }

    void VulkanRendererAPI::Init()
    {
        s_Data = new VulkanRendererData;

        struct QuadVertex
        {
            glm::vec3 Position;
        };

        std::array<QuadVertex, 4> data;
        data[0].Position = { -1, 1, 0 };

        data[1].Position = { -1, -1, 0 };

        data[2].Position = { 1, 1, 0 };

        data[3].Position = { 1, -1, 0 };

        s_Data->QuadVertexBuffer = std::make_shared<VulkanVertexBuffer>( data.data(), 4 * sizeof( QuadVertex ) );
        s_Data->QuadVertexBuffer->Invalidate();

        uint32_t* indices = new uint32_t[6]{
             0, 1, 2, 1, 2, 3,
        };

        s_Data->QuadIndexBuffer = std::make_shared<VulkanIndexBuffer>( indices, 6 * sizeof( unsigned int ) );
        s_Data->QuadIndexBuffer->Invalidate();

        // delete[] indices;
    }

    std::array<VkClearValue, 2> CreateClearValues( const std::shared_ptr<RenderPass>& renderPass )
    {
        const auto                  clearColor = renderPass->GetSpecification().ClearColor;
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = { clearColor.Color.x, clearColor.Color.y, clearColor.Color.z, clearColor.Color.w };
        clearValues[1].depthStencil = { clearColor.DepthStencil.x,
                                        static_cast<uint32_t>( clearColor.DepthStencil.y ) };
        return clearValues;
    }

    std::shared_ptr<VulkanFramebuffer> GetFramebuffer( const std::shared_ptr<RenderPass>& renderPass )
    {
        return sp_cast<Graphic::API::Vulkan::VulkanFramebuffer>(
             renderPass->GetSpecification().TargetFramebuffer );
    }

    std::shared_ptr<VulkanSwapChain> GetSwapChain()
    {
        return sp_cast<Graphic::API::Vulkan::VulkanContext>( Renderer::GetInstance().GetRendererContext() )
             ->GetVulkanSwapChain();
    }

    VkRenderPassBeginInfo CreateRenderPassBeginInfo( const std::shared_ptr<VulkanSwapChain>&   swapChain,
                                                     const std::shared_ptr<VulkanFramebuffer>& framebuffer,
                                                     const std::array<VkClearValue, 2>&        clearValues )
    {
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass            = framebuffer->GetVKRenderPass();
        renderPassBeginInfo.renderArea.offset     = { 0, 0 };
        renderPassBeginInfo.renderArea.extent     = { framebuffer->GetFramebufferWidth(),
                                                      framebuffer->GetFramebufferHeight() };
        renderPassBeginInfo.clearValueCount       = static_cast<uint32_t>( clearValues.size() );
        renderPassBeginInfo.pClearValues          = clearValues.data();
        renderPassBeginInfo.framebuffer           = framebuffer->GetVKFramebuffer();
        return renderPassBeginInfo;
    }

    VkRenderPassBeginInfo CreateRenderPassBeginInfo( const std::shared_ptr<VulkanSwapChain>& swapChain,
                                                     const VkFramebuffer                     framebuffer,
                                                     const std::array<VkClearValue, 2>&      clearValues )
    {
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass            = swapChain->GetRenderPass();
        renderPassBeginInfo.renderArea.offset     = { 0, 0 };
        renderPassBeginInfo.renderArea.extent     = { 1920, 780 };
        renderPassBeginInfo.clearValueCount       = static_cast<uint32_t>( clearValues.size() );
        renderPassBeginInfo.pClearValues          = clearValues.data();
        renderPassBeginInfo.framebuffer           = framebuffer;
        return renderPassBeginInfo;
    }

    Common::BoolResult VulkanRendererAPI::BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass )
    {
        auto        clearValues = CreateClearValues( renderPass );
        auto        framebuffer = GetFramebuffer( renderPass );
        const auto& swapChain   = GetSwapChain();

        m_CompositeFramebuffer = framebuffer;

        SetViewportAndScissor();

        VkRenderPassBeginInfo renderPassBeginInfo =
             CreateRenderPassBeginInfo( swapChain, framebuffer, clearValues );
        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::BeginSwapChainRenderPass()
    {
        uint32_t                    frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();
        std::array<VkClearValue, 2> clearValues{};
        const auto&                 swapChain   = GetSwapChain();
        const auto                  framebuffer = swapChain->GetVKFramebuffers();

        SetViewportAndScissor();

        VkRenderPassBeginInfo renderPassBeginInfo =
             CreateRenderPassBeginInfo( swapChain, framebuffer[frameIndex], clearValues );
        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::EndRenderPass()
    {
        vkCmdEndRenderPass( m_CurrentCommandBuffer );
        return Common::MakeSuccess( true );
    }

    void VulkanRendererAPI::SubmitFullscreenQuad( const std::shared_ptr<Pipeline>& pipeline,
                                                  const std::shared_ptr<Material>& material ) // TODO: clean up
    {
        material->ApplyMaterial();

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        const auto shader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( pipeline->GetSpecification().Shader );
        const auto& desSet = shader->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );

        VkPipelineLayout layout =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipelineLayout();
        vkCmdBindDescriptorSets( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &desSet, 0,
                                 nullptr );

        vkCmdBindPipeline(
             m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipeline() );

        VkDeviceSize offsets[] = { 0 };
        const auto   vbuffer   = s_Data->QuadVertexBuffer->GetVulkanBuffer();
        vkCmdBindVertexBuffers( m_CurrentCommandBuffer, 0, 1, &vbuffer, offsets );

        const auto ibuffer = s_Data->QuadIndexBuffer->GetVulkanBuffer();
        vkCmdBindIndexBuffer( m_CurrentCommandBuffer, ibuffer, 0, VK_INDEX_TYPE_UINT32 );

        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        vkCmdDrawIndexed( m_CurrentCommandBuffer, s_Data->QuadIndexBuffer->GetCount(), 1, 0, 0, 0 );
    }

    void VulkanRendererAPI::ResizeWindowEvent( uint32_t width, uint32_t height,
                                               const std::vector<std::shared_ptr<Framebuffer>>& framebuffers )
    {
        vkDeviceWaitIdle( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice() );

        const auto& swapChain = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
                                     Renderer::GetInstance().GetRendererContext() )
                                     ->GetVulkanSwapChain();

        swapChain->OnResize( width, height );
        for ( auto& fb : framebuffers )
        {
            fb->Resize( width, height );
        }
    }

    void VulkanRendererAPI::SetViewportAndScissor()
    {
        uint32_t windowWidth  = EngineContext::GetInstance().GetCurrentWindowWidth();
        uint32_t windowHeight = EngineContext::GetInstance().GetCurrentWindowHeight();

        VkViewport viewport = {};
        viewport.x          = 0.0f;
        viewport.y          = 0.0f;
        viewport.width      = static_cast<float>( windowWidth );
        viewport.height     = static_cast<float>( windowHeight );
        viewport.minDepth   = 0.0f;
        viewport.maxDepth   = 1.0f;

        VkRect2D scissor = {};
        scissor.offset   = { 0, 0 };
        scissor.extent   = { windowWidth, windowHeight };

        vkCmdSetViewport( m_CurrentCommandBuffer, 0, 1, &viewport );
        vkCmdSetScissor( m_CurrentCommandBuffer, 0, 1, &scissor );
    }

    std::shared_ptr<Framebuffer> VulkanRendererAPI::GetCompositeFramebuffer() const
    {
        return m_CompositeFramebuffer;
    }

    void VulkanRendererAPI::RenderMesh( const std::shared_ptr<Pipeline>& pipeline,
                                        const std::shared_ptr<Mesh>& mesh, const glm::mat4& mvp /*TEMP*/ )
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        /* const auto shader =
              std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( pipeline->GetSpecification().Shader );
         const auto& desSet = shader->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );
         */
        VkPipelineLayout layout =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipelineLayout();
        /*  vkCmdBindDescriptorSets( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
          &desSet, 0, nullptr );*/

        vkCmdBindPipeline(
             m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipeline() );

        VkDeviceSize offsets[] = { 0 };
        const auto   vbuffer =
             sp_cast<API::Vulkan::VulkanVertexBuffer>( mesh->GetVertexBuffer() )->GetVulkanBuffer();
        vkCmdBindVertexBuffers( m_CurrentCommandBuffer, 0, 1, &vbuffer, offsets );

        const auto ibuffer = sp_cast<API::Vulkan::VulkanIndexBuffer>( mesh->GetIndexBuffer() )->GetVulkanBuffer();
        vkCmdBindIndexBuffer( m_CurrentCommandBuffer, ibuffer, 0, VK_INDEX_TYPE_UINT32 );

        // VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        vkCmdPushConstants( m_CurrentCommandBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, &mvp );

        vkCmdDrawIndexed( m_CurrentCommandBuffer, mesh->GetIndexBuffer()->GetCount(), 1, 0, 0, 0 );
    }

    std::shared_ptr<Desert::Graphic::Image2D>
    VulkanRendererAPI::CreateEnvironmentMap( const Common::Filepath& filepath )
    {
        // const auto& outputImage = ConvertPanoramaToCubeMap_4x3( filepath );
        const auto& outputImage = ConvertPanoramaToCubeMap_4x3( filepath );

        // const auto& result = outputImage->GetImagePixels();

        /* const char* outputPath = "output123.hdr";
         const auto  res        = std::get<std::vector<float>>( result );
         int         success =
              stbi_write_hdr( outputPath, outputImage->GetWidth(), outputImage->GetHeight(), 4, res.data() );*/

        return std::shared_ptr<Desert::Graphic::Image2D>( outputImage );
    }

    std::shared_ptr<Desert::Graphic::Image2D>
    VulkanRendererAPI::ConvertPanoramaToCubeMap_4x3( const Common::Filepath& filepath )
    {
        std::shared_ptr<Texture2D> imagePanorama = Texture2D::Create( { true }, filepath );
        imagePanorama->Invalidate();
        const auto& imageVulkan = sp_cast<API::Vulkan::VulkanImage2D>( imagePanorama->GetImage2D() );

        ComputeImageProcessingInfo processingInfo;
        processingInfo.shaderName = "PanoramaToCubemap.glsl";
        processingInfo.width      = kEnvFaceMapSize * 4;
        processingInfo.height     = kEnvFaceMapSize * 3;
        processingInfo.usage      = Core::Formats::ImageUsage::ImageCube;

        return Utils::CreateProcessedImage( imageVulkan, processingInfo );
    }

    std::shared_ptr<Desert::Graphic::Image2D>
    VulkanRendererAPI::CreateDiffuseIrradiance( const Common::Filepath& filepath )
    {
        std::shared_ptr<Texture2D> imagePanorama = Texture2D::Create( { true }, filepath );
        imagePanorama->Invalidate();
        const auto& imageVulkan = sp_cast<API::Vulkan::VulkanImage2D>( imagePanorama->GetImage2D() );

        ComputeImageProcessingInfo processingInfo;
        processingInfo.shaderName = "DiffuseIrradiance.glsl";
        processingInfo.width      = kIrradianceMapSize * 4;
        processingInfo.height     = kIrradianceMapSize * 3;
        processingInfo.usage      = Core::Formats::ImageUsage::ImageCube;

        return Utils::CreateProcessedImage( imageVulkan, processingInfo );
    }

    std::shared_ptr<Desert::Graphic::Image2D>
    VulkanRendererAPI::CreatePrefilteredMap( const Common::Filepath& filepath )
    {
        std::shared_ptr<Texture2D> imagePanorama = Texture2D::Create( { true }, filepath );
        imagePanorama->Invalidate();
        const auto& imageVulkan = sp_cast<API::Vulkan::VulkanImage2D>( imagePanorama->GetImage2D() );

        ComputeImageProcessingInfo processingInfo;
        processingInfo.shaderName = "PrefilterEnvMap.glsl";
        processingInfo.width      = kEnvFaceMapSize * 4;
        processingInfo.height     = kEnvFaceMapSize * 3;
        processingInfo.usage      = Core::Formats::ImageUsage::ImageCube;

        const uint32_t numMipLevels   = Graphic::Utils::CalculateMipCount( kEnvFaceMapSize, kEnvFaceMapSize );
        const float    deltaRoughness = 1.0f / std::max( float( numMipLevels - 1 ), 1.0f );

        processingInfo.pushConstantsCallback =
             [numMipLevels, deltaRoughness]( const std::shared_ptr<PipelineCompute>& pipeline )
        {
            auto vulkanPipeline = sp_cast<API::Vulkan::VulkanPipelineCompute>( pipeline );

            for ( uint32_t level = 1, size = kEnvFaceMapSize / 2; level < numMipLevels; ++level, size /= 2 )
            {
                const SpecularFilterPushConstants pushConstants = {
                     level * deltaRoughness, // roughness
                     level                   // mipLevel
                };

                vulkanPipeline->PushConstant( sizeof( SpecularFilterPushConstants ), (void*)&pushConstants );

                const uint32_t workGroups = std::max<uint32_t>( 1, size / 32 );
                pipeline->Dispatch( workGroups, workGroups, 6 );
            }
        };

        return Utils::CreateProcessedImage( imageVulkan, processingInfo );
    }
#ifdef DESERT_CONFIG_DEBUG
    void VulkanRendererAPI::GenerateMipmaps2D( const std::shared_ptr<Image2D>& image ) const
    {
        auto&       vulkanImage = static_cast<const VulkanImage2D&>( *image );
        const auto& spec        = image->GetImageSpecification();
        VkDevice    device      = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        static auto shader       = Shader::Create( "GenerateMipMap_2D.glsl" );
        const auto& shaderVulkan = sp_cast<VulkanShader>( shader );
        uint32_t    frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();

        auto pipelineCompute = PipelineCompute::Create( shader );
        pipelineCompute->Invalidate();
        auto vulkanPipeline = sp_cast<VulkanPipelineCompute>( pipelineCompute );

        const uint32_t mipLevels = image->GetMipmapLevels();
        const uint32_t width     = image->GetWidth();
        const uint32_t height    = image->GetHeight();

        for ( uint32_t mip = 1; mip < mipLevels; ++mip )
        {
            std::array<VkDescriptorImageInfo, 2> imageInfo = {};

            // Input image (previous mip)
            imageInfo[0].imageView   = ( mip == 1 ) ? vulkanImage.GetVulkanImageInfo().ImageView
                                                    : vulkanImage.GetMipImageView( mip - 1 );
            imageInfo[0].sampler     = vulkanImage.GetVulkanImageInfo().Sampler;
            imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // Output image (current mip)
            imageInfo[1].imageView   = vulkanImage.GetMipImageView( mip );
            imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            // Update descriptor set
            std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

            descriptorWrites[0] =
                 shaderVulkan->GetWriteDescriptorSet( Vulkan::WriteDescriptorType::Sampler2D, 0, 0, frameIndex );
            descriptorWrites[0].dstSet =
                 shaderVulkan->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );
            descriptorWrites[0].pImageInfo = &imageInfo[0];

            descriptorWrites[1] = shaderVulkan->GetWriteDescriptorSet( Vulkan::WriteDescriptorType::StorageImage,
                                                                       1, 0, frameIndex );
            descriptorWrites[1].dstSet =
                 shaderVulkan->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );
            descriptorWrites[1].pImageInfo = &imageInfo[1];

            vkUpdateDescriptorSets( device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr );

            pipelineCompute->Begin();
            const auto cmd = vulkanPipeline->GetCommandBuffer();

            // Transition current mip to GENERAL
            Utils::InsertImageMemoryBarrier( cmd, vulkanImage.GetVulkanImageInfo().Image, 0,
                                             VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                             VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                             VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1 } );

            vulkanPipeline->BindDS( descriptorWrites[0].dstSet );

            // Dispatch compute
            const uint32_t workGroupsX = std::max( 1u, ( width >> mip ) / kWorkGroups );
            const uint32_t workGroupsY = std::max( 1u, ( height >> mip ) / kWorkGroups );
            pipelineCompute->Dispatch( workGroupsX, workGroupsY, 1 );

            Utils::InsertImageMemoryBarrier(
                 cmd, vulkanImage.GetVulkanImageInfo().Image, VK_ACCESS_SHADER_WRITE_BIT,
                 VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1 } );

            pipelineCompute->End();
        }
    }

    void VulkanRendererAPI::GenerateMipmapsCubemap( const std::shared_ptr<Image2D>& image ) const
    {
    }

    Common::BoolResult VulkanRendererAPI::GenerateMipMaps( const std::shared_ptr<Image2D>& image ) const
    {
        const auto spec      = image->GetImageSpecification();
        const bool isCubemap = ( spec.Usage == Core::Formats::ImageUsage::ImageCube );

        if ( isCubemap )
            GenerateMipmapsCubemap( image );
        else
            GenerateMipmaps2D( image );

        return Common::MakeSuccess( true );
    }
#endif

} // namespace Desert::Graphic::API::Vulkan
