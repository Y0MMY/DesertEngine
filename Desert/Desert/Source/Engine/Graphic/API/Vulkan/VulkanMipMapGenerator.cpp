#include <Engine/Graphic/API/Vulkan/VulkanMipMapGenerator.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    static void GenerateMipmapsTO( VkCommandBuffer commandBuffer, VkImage image, VkFormat imageFormat,
                                   uint32_t width, uint32_t height, uint32_t mipLevels,
                                   uint32_t baseArrayLayer = 0, uint32_t layerCount = 1 )
    {
        for ( uint32_t layer = baseArrayLayer; layer < baseArrayLayer + layerCount; layer++ )
        {
            for ( uint32_t i = 1; i < mipLevels; i++ )
            {
                VkImageBlit imageBlit{};

                imageBlit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                imageBlit.srcSubresource.baseArrayLayer = layer;
                imageBlit.srcSubresource.layerCount     = 1;
                imageBlit.srcSubresource.mipLevel       = i - 1;
                imageBlit.srcOffsets[0]                 = { 0, 0, 0 };
                imageBlit.srcOffsets[1].x               = int32_t( std::max( width >> ( i - 1 ), 1u ) );
                imageBlit.srcOffsets[1].y               = int32_t( std::max( height >> ( i - 1 ), 1u ) );
                imageBlit.srcOffsets[1].z               = 1;

                imageBlit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                imageBlit.dstSubresource.baseArrayLayer = layer;
                imageBlit.dstSubresource.layerCount     = 1;
                imageBlit.dstSubresource.mipLevel       = i;
                imageBlit.dstOffsets[0]                 = { 0, 0, 0 };
                imageBlit.dstOffsets[1].x               = int32_t( std::max( width >> i, 1u ) );
                imageBlit.dstOffsets[1].y               = int32_t( std::max( height >> i, 1u ) );
                imageBlit.dstOffsets[1].z               = 1;

                VkImageSubresourceRange mipSubRange = {};
                mipSubRange.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
                mipSubRange.baseArrayLayer          = layer;
                mipSubRange.layerCount              = 1;
                mipSubRange.baseMipLevel            = i;
                mipSubRange.levelCount              = 1;

                Utils::InsertImageMemoryBarrier( commandBuffer, image, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                 mipSubRange );

                vkCmdBlitImage( commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR );

                Utils::InsertImageMemoryBarrier(
                     commandBuffer, image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, mipSubRange );
            }
        }

        VkImageSubresourceRange finalRange = {};
        finalRange.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
        finalRange.baseArrayLayer          = baseArrayLayer;
        finalRange.layerCount              = layerCount;
        finalRange.baseMipLevel            = 0;
        finalRange.levelCount              = mipLevels;

        Utils::InsertImageMemoryBarrier( commandBuffer, image, VK_ACCESS_TRANSFER_READ_BIT,
                                         VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, finalRange );
    }

    Common::BoolResultStr VulkanMipMap2DGeneratorCS::GenerateMips( const std::shared_ptr<Image2D>& image ) const
    {
        return Common::MakeError( "Not impl" );
    }

    Common::BoolResultStr
    VulkanMipMapCubeGeneratorCS::GenerateMips( const std::shared_ptr<ImageCube>& imageCube ) const
    {
        return Common::MakeError( "Not impl" );
    }

    Common::BoolResultStr VulkanMipMap2DGeneratorTO::GenerateMips( const std::shared_ptr<Image2D>& image ) const
    {
        const auto& vulkanImage    = SP_CAST( VulkanImage2D, image );
        const auto& res            = vulkanImage->GetResource();

        const auto cmdAlloc = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
        if ( !cmdAlloc ) return Common::MakeError( cmdAlloc.GetError() );

        VkCommandBuffer commandBuffer = cmdAlloc.GetValue();

        VkImageSubresourceRange baseMipRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 };

        Utils::InsertImageMemoryBarrier( commandBuffer, res.Image, 0, VK_ACCESS_TRANSFER_READ_BIT, res.Layout,
                                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, baseMipRange );

        GenerateMipmapsTO( commandBuffer, res.Image, res.Format, image->GetWidth(), image->GetHeight(),
                           image->GetMipmapLevels() );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( commandBuffer );

        return Common::MakeSuccess( true );
    }

    Common::BoolResultStr
    VulkanMipMapCubeGeneratorTO::GenerateMips( const std::shared_ptr<ImageCube>& imageCube ) const
    {
        const auto& vulkanImage    = SP_CAST( VulkanImageCube, imageCube );
        const auto& res            = vulkanImage->GetResource();

        const auto cmdAlloc = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
        if ( !cmdAlloc ) return Common::MakeError( cmdAlloc.GetError() );

        VkCommandBuffer commandBuffer = cmdAlloc.GetValue();

        VkImageSubresourceRange baseMipRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 6 }; 

        Utils::InsertImageMemoryBarrier( commandBuffer, res.Image, 0, VK_ACCESS_TRANSFER_READ_BIT, res.Layout,
                                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, baseMipRange );

        GenerateMipmapsTO( commandBuffer, res.Image, res.Format, imageCube->GetWidth(), imageCube->GetHeight(),
                           imageCube->GetMipmapLevels(), 0, 6 );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( commandBuffer );

        return Common::MakeSuccess( true );
    }

} // namespace Desert::Graphic::API::Vulkan
