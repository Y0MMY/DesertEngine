#pragma once

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
#define VK_CHECK_RESULT( f )                                                                                      \
    {                                                                                                             \
        VkResult res = ( f );                                                                                     \
        if ( res != VK_SUCCESS )                                                                                  \
        {                                                                                                         \
            LOG_ERROR( "VkResult is 'NONE' in {1}:{2}", __FILE__, __LINE__ );                                     \
            DESERT_VERIFY( false );                                                                               \
        }                                                                                                         \
    }

#define VK_RETURN_RESULT_IF_FALSE( f )                                                                            \
    {                                                                                                             \
        VkResult res = ( f );                                                                                     \
        if ( res != VK_SUCCESS )                                                                                  \
        {                                                                                                         \
            return Common::MakeFormattedError<VkResult>( "VkResult is 'NONE' in {1}:{2}", __FILE__, __LINE__ );   \
        }                                                                                                         \
    }

#define VK_RETURN_RESULT_IF_FALSE_TYPE( type, f )                                                                 \
    {                                                                                                             \
        VkResult res = ( f );                                                                                     \
        if ( res != VK_SUCCESS )                                                                                  \
        {                                                                                                         \
            return Common::MakeFormattedError<type>( "VkResult is 'NONE' in {1}:{2}", __FILE__, __LINE__ );       \
        }                                                                                                         \
    }

#define VK_RETURN_RESULT( f )                                                                                     \
    {                                                                                                             \
        VkResult res = ( f );                                                                                     \
        if ( res != VK_SUCCESS )                                                                                  \
        {                                                                                                         \
            return Common::MakeFormattedError<VkResult>( "VkResult is 'NONE' in {1}:{2}", __FILE__, __LINE__ );   \
        }                                                                                                         \
        else                                                                                                      \
        {                                                                                                         \
            return Common::MakeSuccess( res );                                                                    \
        }                                                                                                         \
    }

    namespace Utils
    {
       inline Common::Result<VkImageView> CreateImageView( VkDevice device, VkImage image, VkFormat format,
                                                     VkImageAspectFlags aspectFlags, VkImageViewType viewType,
                                                     uint32_t layerCount, uint32_t mipLeveles )
        {
            VkImageViewCreateInfo viewInfo = { .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                               .pNext            = VK_NULL_HANDLE,
                                               .image            = image,
                                               .viewType         = viewType,
                                               .format           = format,
                                               .components       = { .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                     .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                     .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                     .a = VK_COMPONENT_SWIZZLE_IDENTITY },
                                               .subresourceRange = { .aspectMask     = aspectFlags,
                                                                     .baseMipLevel   = 0,
                                                                     .levelCount     = mipLeveles,
                                                                     .baseArrayLayer = 0,
                                                                     .layerCount     = layerCount } };

            VkImageView imageView;

            VK_RETURN_RESULT_IF_FALSE_TYPE( VkImageView,
                                            vkCreateImageView( device, &viewInfo, VK_NULL_HANDLE, &imageView ) );

            return Common::MakeSuccess( imageView );
        }
    } // namespace Utils
} // namespace Desert::Graphic::API::Vulkan