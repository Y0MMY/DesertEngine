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

    const inline std::string VkResultToString( VkResult result )
    {
        switch ( result )
        {
            case VK_SUCCESS:
                return "VK_SUCCESS";
            case VK_NOT_READY:
                return "VK_NOT_READY";
            case VK_TIMEOUT:
                return "VK_TIMEOUT";
            case VK_EVENT_SET:
                return "VK_EVENT_SET";
            case VK_EVENT_RESET:
                return "VK_EVENT_RESET";
            case VK_INCOMPLETE:
                return "VK_INCOMPLETE";
            case VK_ERROR_OUT_OF_HOST_MEMORY:
                return "VK_ERROR_OUT_OF_HOST_MEMORY";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            case VK_ERROR_INITIALIZATION_FAILED:
                return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_DEVICE_LOST:
                return "VK_ERROR_DEVICE_LOST";
            case VK_ERROR_MEMORY_MAP_FAILED:
                return "VK_ERROR_MEMORY_MAP_FAILED";
            case VK_ERROR_LAYER_NOT_PRESENT:
                return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_EXTENSION_NOT_PRESENT:
                return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_FEATURE_NOT_PRESENT:
                return "VK_ERROR_FEATURE_NOT_PRESENT";
            case VK_ERROR_INCOMPATIBLE_DRIVER:
                return "VK_ERROR_INCOMPATIBLE_DRIVER";
            case VK_ERROR_TOO_MANY_OBJECTS:
                return "VK_ERROR_TOO_MANY_OBJECTS";
            case VK_ERROR_FORMAT_NOT_SUPPORTED:
                return "VK_ERROR_FORMAT_NOT_SUPPORTED";
            default:
                return "Unknown VkResult";
        }
    }

    namespace Utils
    {
        inline Common::Result<VkImageView> CreateImageView( VkDevice device, VkImage image, VkFormat format,
                                                            VkImageAspectFlags aspectFlags,
                                                            VkImageViewType viewType, uint32_t layerCount,
                                                            uint32_t mipLeveles )
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

            // VK_CHECK_RESULT( vkSetDebugUtilsObjectNameEXT( device, &nameInfo ) );
        }
    } // namespace VKUtils

} // namespace Desert::Graphic::API::Vulkan