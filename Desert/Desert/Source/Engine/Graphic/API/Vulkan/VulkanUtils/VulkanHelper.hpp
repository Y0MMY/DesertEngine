#pragma once

#include <vulkan/vulkan.hpp>

inline PFN_vkSetDebugUtilsObjectNameEXT
     fpSetDebugUtilsObjectNameEXT; // Making it static randomly sets it to nullptr for some reason.
inline PFN_vkCmdBeginDebugUtilsLabelEXT fpCmdBeginDebugUtilsLabelEXT; // command-buffer regions (RenderDoc tree)
inline PFN_vkCmdEndDebugUtilsLabelEXT   fpCmdEndDebugUtilsLabelEXT;

namespace Desert::Graphic::API::Vulkan
{
    void VulkanLoadDebugUtilsExtensions( VkInstance instance );

#define VK_CHECK_RESULT( f )                                                                                      \
    {                                                                                                             \
        VkResult res = ( f );                                                                                     \
        if ( res != VK_SUCCESS )                                                                                  \
        {                                                                                                         \
            LOG_ERROR( "VkResult is '{}' in {}:{}", VkResultToString( res ), __FILE__, __LINE__ );                \
            DESERT_VERIFY( false );                                                                               \
        }                                                                                                         \
    }

#define VK_CHECK_RESULT_BOOL( f )                                                                                 \
    {                                                                                                             \
        VkResult res = ( f );                                                                                     \
        if ( res != VK_SUCCESS )                                                                                  \
        {                                                                                                         \
            LOG_ERROR( "VkResult is '{}' in {}:{}", VkResultToString( res ), __FILE__, __LINE__ );                \
            return Common::MakeFormattedError<bool>( "VkResult is '{}' in {}:{}", VkResultToString( res ),         \
                                                     __FILE__, __LINE__ );                                        \
        }                                                                                                         \
    }

#define VK_RETURN_RESULT_IF_FALSE( f )                                                                            \
    {                                                                                                             \
        VkResult res = ( f );                                                                                     \
        if ( res != VK_SUCCESS )                                                                                  \
        {                                                                                                         \
            return Common::MakeFormattedError<VkResult>( "VkResult is '{}' in {}:{}", VkResultToString( res ),    \
                                                         __FILE__, __LINE__ );                                    \
        }                                                                                                         \
    }

#define VK_RETURN_RESULT_IF_FALSE_TYPE( type, f )                                                                 \
    {                                                                                                             \
        VkResult res = ( f );                                                                                     \
        if ( res != VK_SUCCESS )                                                                                  \
        {                                                                                                         \
            return Common::MakeFormattedError<type>( "VkResult is '{}' in {}:{}", VkResultToString( res ),        \
                                                     __FILE__, __LINE__ );                                        \
        }                                                                                                         \
    }

#define VK_RETURN_RESULT( f )                                                                                     \
    {                                                                                                             \
        VkResult res = ( f );                                                                                     \
        if ( res != VK_SUCCESS )                                                                                  \
        {                                                                                                         \
            return Common::MakeFormattedError<VkResult>( std::string(#f ) + std::string( " -> result: {}" ),    \
                                                         VkResultToString( res ) );                               \
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

        Common::ResultStr<VkImageView> CreateImageView( VkDevice device, VkImage image, VkFormat format,
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

        // Opens a named region in the command buffer — RenderDoc/Xcode show these as a tree of
        // passes. Always paired with EndDebugLabel; no-ops when debug utils are unavailable.
        inline static void BeginDebugLabel( VkCommandBuffer cmdBuffer, const char* name )
        {
            VkDebugUtilsLabelEXT label{};
            label.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label.pLabelName = name;
            fpCmdBeginDebugUtilsLabelEXT( cmdBuffer, &label );
        }

        inline static void EndDebugLabel( VkCommandBuffer cmdBuffer )
        {
            fpCmdEndDebugUtilsLabelEXT( cmdBuffer );
        }
    } // namespace VKUtils

} // namespace Desert::Graphic::API::Vulkan