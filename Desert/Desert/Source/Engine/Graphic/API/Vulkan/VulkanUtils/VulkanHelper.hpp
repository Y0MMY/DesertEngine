#pragma once

#include <vulkan/vulkan.hpp>

inline PFN_vkSetDebugUtilsObjectNameEXT
     fpSetDebugUtilsObjectNameEXT; // Making it static randomly sets it to nullptr for some reason.

namespace Desert::Graphic::API::Vulkan
{
    void VulkanLoadDebugUtilsExtensions( VkInstance instance );

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
            return Common::MakeFormattedError<VkResult>( "VkResult is 'NONE' in {1}:{2}. Type: {3} ", __FILE__,   \
                                                         __LINE__, VkResultToString( res ) );                     \
        }                                                                                                         \
        else                                                                                                      \
        {                                                                                                         \
            return Common::MakeSuccess( res );                                                                    \
        }                                                                                                         \
    }

    const std::string VkResultToString( VkResult result );

    namespace Utils
    {
        void InsertImageMemoryBarrier( VkCommandBuffer cmdBuf, VkImage Image, VkFormat Format,
                                       VkImageLayout OldLayout, VkImageLayout NewLayout, uint32_t layers = 1,
                                       uint32_t mipLevels = 1 );

        void InsertImageMemoryBarrier( VkCommandBuffer cmdbuffer, VkImage image, VkAccessFlags srcAccessMask,
                                       VkAccessFlags dstAccessMask, VkImageLayout oldImageLayout,
                                       VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask,
                                       VkPipelineStageFlags    dstStageMask,
                                       VkImageSubresourceRange subresourceRange );

        Common::Result<VkImageView> CreateImageView( VkDevice device, VkImage image, VkFormat format,
                                                     VkImageAspectFlags aspectFlags, VkImageViewType viewType,
                                                     uint32_t layerCount, uint32_t mipLeveles );
    } // namespace Utils

    namespace VKUtils
    {
        inline static void SetDebugUtilsObjectName( const VkDevice device, const VkObjectType objectType,
                                                    const std::string& name, const void* handle )
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo;
            nameInfo.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType   = objectType;
            nameInfo.pObjectName  = name.c_str();
            nameInfo.objectHandle = (uint64_t)handle;
            nameInfo.pNext        = VK_NULL_HANDLE;

            VK_CHECK_RESULT( fpSetDebugUtilsObjectNameEXT( device, &nameInfo ) );
        }
    } // namespace VKUtils

} // namespace Desert::Graphic::API::Vulkan