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
#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>

#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Texture.hpp>

#include <Engine/Core/EngineContext.hpp>
#include "stb_image/stb_image_write.h"

#include <glm/glm.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        bool ShouldUseDynamicLineWidth( const std::shared_ptr<Pipeline>& pipeline )
        {
            const auto& spec = pipeline->GetSpecification();
            if ( spec.PolygonMode == PrimitivePolygonMode::Wireframe || Graphic::PrimitiveIsLine( spec.Topology ) )
            {
                return true;
            }

            return false;
        }
    } // namespace

    static constexpr uint32_t kEnvFaceMapSize    = 1024;
    static constexpr uint32_t kIrradianceMapSize = 32;
    static constexpr uint32_t kBRDF_LUT_Size     = 256;
    static constexpr uint32_t kWorkGroups        = 32;

    struct SpecularFilterPushConstants
    {
        uint32_t mipLevel;
        float    roughness;
    };

    struct ComputeImageProcessingInfo
    {
        bool                       CalculateMips;
        std::string                shaderName;
        uint32_t                   width;
        uint32_t                   height;
        Core::Formats::ImageFormat format = Core::Formats::ImageFormat::RGBA32F;
        std::function<void( const std::shared_ptr<PipelineCompute>& )> pushConstantsCallback = nullptr;
    };

    namespace Utils
    {
        std::shared_ptr<ImageCube> CreateProcessedImage( const std::shared_ptr<VulkanImage2D>& inputImage,
                                                         const ComputeImageProcessingInfo&     processingInfo )
        {
            static std::unordered_map<std::string, std::shared_ptr<Shader>> shaderCache;

            auto& shader = shaderCache[processingInfo.shaderName];
            if ( !shader )
            {
                shader = Shader::Create( processingInfo.shaderName );
            }

            const uint32_t mips =
                 Graphic::Utils::CalculateMipCount( processingInfo.width / 4, processingInfo.height / 3 );

            Core::Formats::ImageCubeSpecification outputImageInfo = {
                 .Tag        = inputImage->GetImageSpecification().Tag + "_" + processingInfo.shaderName,
                 .Width      = processingInfo.width,
                 .Height     = processingInfo.height,
                 .Format     = processingInfo.format,
                 .Mips       = processingInfo.CalculateMips ? mips : 1,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };

            const std::shared_ptr<ImageCube> outputImage = ImageCube::Create( outputImageInfo, nullptr );
            const auto& outputImageVulkan                = sp_cast<API::Vulkan::VulkanImageCube>( outputImage );
            outputImageVulkan->RT_Invalidate();

            uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

            auto pipelineCompute = Graphic::PipelineCompute::Create( shader );
            auto vulkanPipeline  = sp_cast<VulkanPipelineCompute>( pipelineCompute );
            vulkanPipeline->Invalidate();

            auto descriptorSetResult = vulkanPipeline->GetDescriptorSet( frameIndex, 0 );
            if ( !descriptorSetResult.IsSuccess() )
            {
                // return Common::MakeError( "Failed to allocate descriptor set" );
            }

            std::array<VkDescriptorImageInfo, 2> imageInfo = {};

            // Input image
            imageInfo[0].imageView   = inputImage->GetVulkanImageInfo().ImageInfo.imageView;
            imageInfo[0].sampler     = inputImage->GetVulkanImageInfo().ImageInfo.sampler;
            imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // Output image
            imageInfo[1].imageView   = outputImageVulkan->GetVulkanImageInfo().ImageInfo.imageView;
            imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            std::vector<VkWriteDescriptorSet> descriptorWrites;

            descriptorWrites.push_back( DescriptorSetBuilder::GetSamplerCubeWDS(
                 sp_cast<VulkanShader>( shader ), frameIndex, 0, 0, 1, &imageInfo[0] ) );

            descriptorWrites.push_back( DescriptorSetBuilder::GetStorageWDS(
                 sp_cast<VulkanShader>( shader ), frameIndex, 0, 1, 1, &imageInfo[1] ) );

            vulkanPipeline->UpdateDescriptorSet( frameIndex, descriptorWrites, descriptorSetResult.GetValue() );

            pipelineCompute->Begin();

            const auto cmd = vulkanPipeline->GetCommandBuffer();

            vulkanPipeline->BindDescriptorSets( descriptorSetResult.GetValue(), frameIndex );

            if ( processingInfo.pushConstantsCallback )
            {
                processingInfo.pushConstantsCallback( pipelineCompute );
            }
            else
            {
                const uint32_t workGroupsX = processingInfo.width / kWorkGroups;
                const uint32_t workGroupsY = processingInfo.height / kWorkGroups;
                const uint32_t workGroupsZ = 6;

                pipelineCompute->Dispatch( workGroupsX, workGroupsY, workGroupsZ );
            }

            VkImageSubresourceRange subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                         .baseMipLevel   = 0,
                                                         .levelCount     = outputImageInfo.Mips,
                                                         .baseArrayLayer = 0,
                                                         .layerCount     = 6 };

            Utils::InsertImageMemoryBarrier(
                 cmd, outputImageVulkan->GetVulkanImageInfo().Image, 0, VK_ACCESS_SHADER_READ_BIT,
                 VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange );

            pipelineCompute->End();

            inputImage->Release();

            return outputImage;
        }

    } // namespace Utils

    Common::BoolResult VulkanRendererAPI::BeginFrame()
    {
        if ( m_CurrentCommandBuffer != nullptr )
        {
            return Common::MakeError<bool>( "BeginFrame(): Error! Have you call EndFrame() ?" );
        }

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        m_DescriptorManager->CleanupFrame( frameIndex );

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

    Common::BoolResult VulkanRendererAPI::PrepareNextFrame()
    {
        // static_cast<VulkanContext*>( Renderer::GetInstance().GetRendererContext().get() )->PrepareNextFrame();

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::PresentFinalImage()
    {
        // static_cast<VulkanContext*>( Renderer::GetInstance().GetRendererContext().get() )->PresentFinalImage();

        return Common::MakeSuccess( true );
    }

    void VulkanRendererAPI::Init()
    {
        m_DescriptorManager = std::make_unique<VulkanDescriptorManager>();
        m_DescriptorManager->Initialize( EngineContext::GetInstance().GetFramesInFlight() );
    }

    std::shared_ptr<VulkanFramebuffer> GetFramebuffer( const std::shared_ptr<RenderPass>& renderPass )
    {
        return sp_cast<Graphic::API::Vulkan::VulkanFramebuffer>(
             renderPass->GetSpecification().TargetFramebuffer );
    }

    const std::unique_ptr<VulkanSwapChain>& GetSwapChain()
    {
        return static_cast<VulkanContext*>( Renderer::GetInstance().GetRendererContext().get() )
             ->GetVulkanSwapChain();
    }

    VkRenderPassBeginInfo CreateRenderPassBeginInfo( const std::unique_ptr<VulkanSwapChain>&   swapChain,
                                                     const std::shared_ptr<VulkanFramebuffer>& framebuffer,
                                                     const std::vector<VkClearValue>&          clearValues )
    {
        const auto width  = framebuffer->GetFramebufferWidth();
        const auto height = framebuffer->GetFramebufferHeight();

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass            = framebuffer->GetVKRenderPass();
        renderPassBeginInfo.renderArea.offset     = { 0, 0 };
        renderPassBeginInfo.renderArea.extent     = { width, height };
        renderPassBeginInfo.clearValueCount       = clearValues.size();
        renderPassBeginInfo.pClearValues          = clearValues.data();
        renderPassBeginInfo.framebuffer           = framebuffer->GetVKFramebuffer();
        return renderPassBeginInfo;
    }

    VkRenderPassBeginInfo CreateRenderPassBeginInfo( const std::unique_ptr<VulkanSwapChain>& swapChain,
                                                     const VkFramebuffer                     framebuffer,
                                                     const std::array<VkClearValue, 2>&      clearValues )
    {
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass            = swapChain->GetRenderPass();
        renderPassBeginInfo.renderArea.offset     = { 0, 0 };
        renderPassBeginInfo.renderArea.extent     = { swapChain->GetWidth(), swapChain->GetHeight() };
        renderPassBeginInfo.clearValueCount       = static_cast<uint32_t>( clearValues.size() );
        renderPassBeginInfo.pClearValues          = clearValues.data();
        renderPassBeginInfo.framebuffer           = framebuffer;
        return renderPassBeginInfo;
    }

    Common::BoolResult VulkanRendererAPI::BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass )
    {
        auto clearValues =
             std::static_pointer_cast<VulkanFramebuffer>( renderPass->GetSpecification().TargetFramebuffer )
                  ->GetClearValues();
        auto        framebuffer = GetFramebuffer( renderPass );
        const auto& swapChain   = GetSwapChain();

        m_CompositeFramebuffer = framebuffer;

        VkRenderPassBeginInfo renderPassBeginInfo =
             CreateRenderPassBeginInfo( swapChain, framebuffer, clearValues );
        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        SetViewportAndScissor();
        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::BeginSwapChainRenderPass()
    {
        uint32_t                    frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();
        std::array<VkClearValue, 2> clearValues{};
        const auto&                 swapChain   = GetSwapChain();
        const auto                  framebuffer = swapChain->GetVKFramebuffers();

        VkRenderPassBeginInfo renderPassBeginInfo =
             CreateRenderPassBeginInfo( swapChain, framebuffer[frameIndex], clearValues );
        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        SetViewportAndScissor();
        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::EndRenderPass()
    {
        vkCmdEndRenderPass( m_CurrentCommandBuffer );
        return Common::MakeSuccess( true );
    }

    void VulkanRendererAPI::SubmitFullscreenQuad( const std::shared_ptr<Pipeline>&         pipeline,
                                                  const std::shared_ptr<MaterialExecutor>& material )
    {
        uint32_t    frameIndex          = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& vulkanShader        = sp_cast<API::Vulkan::VulkanShader>( material->GetShader() );
        auto        descriptorSetResult = m_DescriptorManager->GetDescriptorSet( vulkanShader, frameIndex );
        if ( !descriptorSetResult.IsSuccess() )
        {
            LOG_ERROR( "Failed to get descriptor set: {}", descriptorSetResult.GetError() );
            return;
        }

        material->Apply();

        const auto& vulkanLayout  = std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline );
        const auto& descriptorSet = descriptorSetResult.GetValue();
        m_DescriptorManager->BindDescriptorSets( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 vulkanLayout->GetVkPipelineLayout(), vulkanShader, frameIndex );

        vkCmdBindPipeline( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vulkanLayout->GetVkPipeline() );

        vkCmdDraw( m_CurrentCommandBuffer, 6, 1, 0, 0 );
    }

    void VulkanRendererAPI::ResizeWindowEvent( uint32_t width, uint32_t height )
    {
        if ( width == 0 && height == 0 )
            return;
        const auto device       = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        uint32_t   currentIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        const auto& pool = CommandBufferAllocator::GetInstance().GetCommandGraphicPool();
        VK_CHECK_RESULT( vkResetCommandPool( device, pool[currentIndex], 0 ) );

        const auto& swapChain = static_cast<VulkanContext*>( Renderer::GetInstance().GetRendererContext().get() )
                                     ->GetVulkanSwapChain();
        if ( width == swapChain->GetWidth() && height == swapChain->GetHeight() )
            return;

        swapChain->OnResize( width, height );

        auto commandBuffer = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );

        for ( auto& image : swapChain->GetSwapChainVKImage() )
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout                   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                       = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier( commandBuffer.GetValue(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                  &barrier );
        }

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( commandBuffer.GetValue() );
    }

    void VulkanRendererAPI::SetViewportAndScissor()
    {
        uint32_t windowWidth  = Common::CommonContext::GetInstance().GetCurrentWindowWidth();
        uint32_t windowHeight = Common::CommonContext::GetInstance().GetCurrentWindowHeight();

        VkViewport viewport = {};
        viewport.x          = 0.0f;
        viewport.y          = (float)windowHeight;
        viewport.height     = -(float)windowHeight;
        viewport.width      = (float)windowWidth;
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
        if ( const auto& framebuffer = m_CompositeFramebuffer.lock() ) [[likely]]
        {
            return framebuffer;
        }
        return nullptr;
    }

    void VulkanRendererAPI::RenderMesh( const std::shared_ptr<Pipeline>&         pipeline,
                                        const std::shared_ptr<Mesh>&             mesh,
                                        const std::shared_ptr<MaterialExecutor>& material )
    {

        uint32_t    frameIndex          = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& vulkanShader        = sp_cast<API::Vulkan::VulkanShader>( material->GetShader() );
        auto        descriptorSetResult = m_DescriptorManager->GetDescriptorSet( vulkanShader, frameIndex );
        if ( !descriptorSetResult.IsSuccess() )
        {
            LOG_ERROR( "Failed to get descriptor set: {}", descriptorSetResult.GetError() );
            return;
        }

        material->Apply();

        const auto& vulkanLayout  = std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline );
        const auto& descriptorSet = descriptorSetResult.GetValue();
        m_DescriptorManager->BindDescriptorSets( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 vulkanLayout->GetVkPipelineLayout(), vulkanShader, frameIndex );

        vkCmdBindPipeline( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vulkanLayout->GetVkPipeline() );

        VkDeviceSize offsets[] = { 0 };
        const auto   vbuffer =
             sp_cast<API::Vulkan::VulkanVertexBuffer>( mesh->GetVertexBuffer() )->GetVulkanBuffer();
        vkCmdBindVertexBuffers( m_CurrentCommandBuffer, 0, 1, &vbuffer, offsets );

        const auto ibuffer = sp_cast<API::Vulkan::VulkanIndexBuffer>( mesh->GetIndexBuffer() )->GetVulkanBuffer();
        vkCmdBindIndexBuffer( m_CurrentCommandBuffer, ibuffer, 0, VK_INDEX_TYPE_UINT32 );

        if ( ShouldUseDynamicLineWidth( pipeline ) )
        {
            vkCmdSetLineWidth( m_CurrentCommandBuffer, pipeline->GetSpecification().LineWidth );
        }

        const auto& pcBuffer = material->GetPushConstantBuffer();
        if ( pcBuffer.Size )
        {
            vkCmdPushConstants( m_CurrentCommandBuffer, vulkanLayout->GetVkPipelineLayout(),
                                VK_SHADER_STAGE_VERTEX_BIT, 0, pcBuffer.Size, pcBuffer.Data );
        }

        const auto& submeshes = mesh->GetSubmeshes();
        for ( const auto& submesh : submeshes )
        {
            vkCmdDrawIndexed( m_CurrentCommandBuffer, submesh.IndexCount, 1, submesh.IndexOffset,
                              submesh.VertexOffset, 0 );
        }
    }

#ifdef DESERT_CONFIG_DEBUG
    PBRTextures VulkanRendererAPI::CreateEnvironmentMap( const Common::Filepath& filepath )
    {
        const auto& envMap     = ConvertPanoramaToCubeMap_4x3( filepath, false );
        const auto& irradiance = CreateDiffuseIrradiance( filepath );

        const auto& preFiltered = ConvertPanoramaToCubeMap_4x3( filepath, true );
        CreatePrefilteredMap( preFiltered );
        return { envMap, irradiance, preFiltered };
    }

    std::shared_ptr<Desert::Graphic::ImageCube>
    VulkanRendererAPI::ConvertPanoramaToCubeMap_4x3( const Common::Filepath& filepath, bool calculateMips )
    {
        std::shared_ptr<Texture2D> imagePanorama = Texture2D::Create( { true }, filepath );
        imagePanorama->Invalidate();
        const auto& imageVulkan = sp_cast<API::Vulkan::VulkanImage2D>( imagePanorama->GetImage2D() );

        ComputeImageProcessingInfo processingInfo;
        processingInfo.shaderName    = "PanoramaToCubemap.glsl";
        processingInfo.width         = kEnvFaceMapSize * 4;
        processingInfo.height        = kEnvFaceMapSize * 3;
        processingInfo.CalculateMips = calculateMips;

        return Utils::CreateProcessedImage( imageVulkan, processingInfo );
    }

    std::shared_ptr<Desert::Graphic::ImageCube>
    VulkanRendererAPI::CreateDiffuseIrradiance( const Common::Filepath& filepath )
    {
        std::shared_ptr<Texture2D> imagePanorama = Texture2D::Create( { false }, filepath );
        imagePanorama->Invalidate();
        const auto& imageVulkan = sp_cast<API::Vulkan::VulkanImage2D>( imagePanorama->GetImage2D() );

        ComputeImageProcessingInfo processingInfo;
        processingInfo.shaderName    = "DiffuseIrradiance.glsl";
        processingInfo.width         = kIrradianceMapSize * 4;
        processingInfo.height        = kIrradianceMapSize * 3;
        processingInfo.CalculateMips = false;

        return Utils::CreateProcessedImage( imageVulkan, processingInfo );
    }

    Common::BoolResult VulkanRendererAPI::CreatePrefilteredMap( const std::shared_ptr<ImageCube>& imageCube )
    {
        const auto generatorMips = MipMapCubeGenerator::Create( MipGenStrategy::ComputeShader );
        generatorMips->GenerateMips( imageCube );

        const auto& vulkanImage = static_cast<const VulkanImageCube&>( *imageCube );
        const auto& spec        = imageCube->GetImageSpecification();

        uint32_t    frameIndex      = Renderer::GetInstance().GetCurrentFrameIndex();
        static auto shader          = Shader::Create( "PrefilterEnvMap.glsl" );
        auto        pipelineCompute = PipelineCompute::Create( shader );
        auto        vulkanPipeline  = sp_cast<VulkanPipelineCompute>( pipelineCompute );
        pipelineCompute->Invalidate();

        auto descriptorSetResult = vulkanPipeline->GetDescriptorSet( frameIndex, 0 );
        if ( !descriptorSetResult.IsSuccess() )
        {
            return Common::MakeError( "Failed to allocate descriptor set" );
        }

        const uint32_t mipLevels      = imageCube->GetMipmapLevels();
        const uint32_t width          = imageCube->GetWidth();
        const uint32_t height         = imageCube->GetHeight();
        const float    deltaRoughness = 1.0f / std::max( float( mipLevels - 1 ), 1.0f );

        for ( uint32_t mip = 1; mip < mipLevels; ++mip )
        {
            std::array<VkDescriptorImageInfo, 2> imageInfo = {};

            // Input image (previous mip)
            imageInfo[0].imageView   = ( mip == 1 ) ? vulkanImage.GetVulkanImageInfo().ImageInfo.imageView
                                                    : vulkanImage.GetMipImageView( mip - 1 );
            imageInfo[0].sampler     = vulkanImage.GetVulkanImageInfo().ImageInfo.sampler;
            imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // Output image (current mip)
            imageInfo[1].imageView   = vulkanImage.GetMipImageView( mip );
            imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            std::vector<VkWriteDescriptorSet> descriptorWrites;

            descriptorWrites.push_back( DescriptorSetBuilder::GetSamplerCubeWDS(
                 sp_cast<VulkanShader>( shader ), frameIndex, 0, 0, 1, &imageInfo[0] ) );

            descriptorWrites.push_back( DescriptorSetBuilder::GetStorageWDS(
                 sp_cast<VulkanShader>( shader ), frameIndex, 0, 1, 1, &imageInfo[1] ) );

            vulkanPipeline->UpdateDescriptorSet( frameIndex, descriptorWrites, descriptorSetResult.GetValue() );

            pipelineCompute->Begin();
            const auto cmd = vulkanPipeline->GetCommandBuffer();

            Utils::InsertImageMemoryBarrier( cmd, vulkanImage.GetVulkanImageInfo().Image, 0,
                                             VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                             VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                             VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6 } );

            vulkanPipeline->BindDescriptorSets( descriptorSetResult.GetValue(), frameIndex );

            const SpecularFilterPushConstants pushConstants = {
                 mip - 1,              // mipLevel
                 mip * deltaRoughness, // roughness
            };
            vulkanPipeline->PushConstant( sizeof( SpecularFilterPushConstants ), (void*)&pushConstants );

            const uint32_t workGroupsX = std::max( 1u, ( width >> mip ) / kWorkGroups );
            const uint32_t workGroupsY = std::max( 1u, ( height >> mip ) / kWorkGroups );
            pipelineCompute->Dispatch( workGroupsX, workGroupsY, 6 );

            Utils::InsertImageMemoryBarrier(
                 cmd, vulkanImage.GetVulkanImageInfo().Image, VK_ACCESS_SHADER_WRITE_BIT, 0,
                 VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                 VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6 } );

            pipelineCompute->End();
        }

        return Common::MakeSuccess( true );
    }

#endif

    void VulkanRendererAPI::Shutdown()
    {
        vkDeviceWaitIdle( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice() );

        m_DescriptorManager.reset();
    }

    VkCommandBuffer VulkanRendererAPI::GetCurrentCmdBuffer() const
    {
        return m_CurrentCommandBuffer;
    }
} // namespace Desert::Graphic::API::Vulkan
