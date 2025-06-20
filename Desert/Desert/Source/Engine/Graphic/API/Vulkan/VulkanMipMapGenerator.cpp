#include <Engine/Graphic/API/Vulkan/VulkanMipMapGenerator.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic::API::Vulkan
{
    static constexpr uint32_t kWorkGroups = 32;

    namespace Utils
    {
        template <typename VulkanImageType>
        Common::BoolResult GenerateMipmaps( const VulkanImageType& vulkanImage, uint32_t width, uint32_t height,
                                            uint32_t mipLevels, uint32_t layers, const std::string& shaderName )
        {
            static auto shader       = Shader::Create( shaderName );
            const auto& shaderVulkan = sp_cast<VulkanShader>( shader );
            uint32_t    frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();

            auto pipelineCompute = PipelineCompute::Create( shader );
            pipelineCompute->Invalidate();
            auto vulkanPipeline = sp_cast<VulkanPipelineCompute>( pipelineCompute );

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
                     DescriptorSetBuilder::GetSamplerWDS( shaderVulkan, frameIndex, 0U, 0, 1U, &imageInfo[0] );

                descriptorWrites[1] =
                     DescriptorSetBuilder::GetStorageWDS( shaderVulkan, frameIndex, 0U, 1, 1U, &imageInfo[1] );

                vkUpdateDescriptorSets( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(),
                                        descriptorWrites.size(), descriptorWrites.data(), 0, nullptr );

                pipelineCompute->Begin();
                const auto cmd = sp_cast<VulkanPipelineCompute>( pipelineCompute )->GetCommandBuffer();

                // Transition current mip to GENERAL
                Utils::InsertImageMemoryBarrier(
                     cmd, vulkanImage.GetVulkanImageInfo().Image, 0, VK_ACCESS_SHADER_WRITE_BIT,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, layers } );

                vulkanPipeline->BindDS( descriptorWrites[0].dstSet );

                // Dispatch compute
                const uint32_t workGroupsX = std::max( 1u, ( width >> mip ) / kWorkGroups );
                const uint32_t workGroupsY = std::max( 1u, ( height >> mip ) / kWorkGroups );
                pipelineCompute->Dispatch( workGroupsX, workGroupsY, layers );

                // Transition current mip to SHADER_READ_ONLY_OPTIMAL
                Utils::InsertImageMemoryBarrier(
                     cmd, vulkanImage.GetVulkanImageInfo().Image, VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, layers } );

                pipelineCompute->End();
            }

            return Common::MakeSuccess( true );
        }
    } // namespace Utils

    Common::BoolResult VulkanMipMap2DGeneratorCS::GenerateMips( const std::shared_ptr<Image2D>& image ) const
    {
        auto& vulkanImage = static_cast<const VulkanImage2D&>( *image );

        return Utils::GenerateMipmaps<VulkanImage2D>( vulkanImage, image->GetWidth(), image->GetHeight(),
                                                      image->GetMipmapLevels(), 1, "GenerateMipMap_2D.glsl" );
    }

    Common::BoolResult
    VulkanMipMapCubeGeneratorCS::GenerateMips( const std::shared_ptr<ImageCube>& imageCube ) const
    {

        return Utils::GenerateMipmaps<VulkanImageCube>(
             static_cast<const VulkanImageCube&>( *imageCube ), imageCube->GetWidth(), imageCube->GetHeight(),
             imageCube->GetMipmapLevels(), 6, std::string( "GenerateMipMap_Cube.glsl" ) );
    }

} // namespace Desert::Graphic::API::Vulkan