#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <vulkan/vulkan.h>

#ifndef VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#define VK_KHR_WIN32_SURFACE_EXTENSION_NAME "VK_KHR_win32_surface"
#endif

namespace Desert::Graphic::API::Vulkan
{
    static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugReportCallback( VkDebugReportFlagsEXT      flags,
                                                                     VkDebugReportObjectTypeEXT objectType,
                                                                     uint64_t object, size_t location,
                                                                     int32_t messageCode, const char* pLayerPrefix,
                                                                     const char* pMessage, void* pUserData )
    {
        (void)flags;
        (void)object;
        (void)location;
        (void)messageCode;
        (void)pUserData;
        (void)pLayerPrefix; // Unused arguments
        LOG_WARN( "VulkanDebugCallback:\n  Object Type: {0}\n  Message: {1}", (int)objectType, pMessage );
        return VK_FALSE;
    }

#ifdef DESERT_CONFIG_DEBUG
    static bool s_DebugValidation = true;
#else
    static bool s_DebugValidation = false;
#endif

    VulkanContext::VulkanContext( const std::shared_ptr<Window>& window ) : m_Window( window )
    {
        CreateVKInstance();
    }

    Common::Result<VkResult> VulkanContext::CreateVKInstance()
    {
        LOG_TRACE( "VulkanRenderingContext::CreateVKInstance()" );
        DESERT_VERIFY( glfwVulkanSupported(), "GLFW must support Vulkan API" );
        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "Hello Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION( 1, 2, 0 );
        appInfo.pEngineName        = "No Engine";
        appInfo.engineVersion      = VK_MAKE_VERSION( 1, 3, 0 );
        appInfo.apiVersion         = VK_API_VERSION_1_3;

        std::vector<const char*> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME,
                                                        VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
        if ( s_DebugValidation )
        {
            instanceExtensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
            instanceExtensions.push_back( VK_EXT_DEBUG_REPORT_EXTENSION_NAME );
            instanceExtensions.push_back( VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME );
        }

        VkInstanceCreateInfo createInfo{};
        createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo        = &appInfo;
        createInfo.enabledExtensionCount   = (uint32_t)instanceExtensions.size();
        createInfo.ppEnabledExtensionNames = instanceExtensions.data();
        createInfo.enabledLayerCount       = 0;

        if ( s_DebugValidation )
        {
            const char* validationLayers = "VK_LAYER_KHRONOS_validation";

            uint32_t layerCount;
            vkEnumerateInstanceLayerProperties( &layerCount, nullptr );

            std::vector<VkLayerProperties> availableLayers( layerCount );
            vkEnumerateInstanceLayerProperties( &layerCount, availableLayers.data() );

            bool validationLayerPresent = false;
            LOG_INFO( "Vulkan Instance Layers:" );
            for ( const VkLayerProperties& layer : availableLayers )
            {
                LOG_INFO( "  {0}", layer.layerName );
                for ( const auto& layerProperties : availableLayers )
                {
                    if ( strcmp( layer.layerName, validationLayers ) == 0 )
                    {
                        validationLayerPresent = true;
                        break;
                    }
                }
            }

            if ( validationLayerPresent )
            {
                createInfo.ppEnabledLayerNames = &validationLayers;
                createInfo.enabledLayerCount   = 1;
            }
            else
            {
                LOG_ERROR( "Validation layer VK_LAYER_KHRONOS_validation not present, validation is disabled" );
            }
        }

        VK_RETURN_RESULT_IF_FALSE( vkCreateInstance( &createInfo, nullptr, &s_VulkanInstance ) );
        if ( s_DebugValidation )
        {
            auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
                 s_VulkanInstance, "vkCreateDebugReportCallbackEXT" );
            DESERT_VERIFY( vkCreateDebugReportCallbackEXT != NULL, "" );
            VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
            debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
            debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
                                    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
            debug_report_ci.pfnCallback = VulkanDebugReportCallback;
            debug_report_ci.pUserData   = NULL;
            VK_CHECK_RESULT( vkCreateDebugReportCallbackEXT( s_VulkanInstance, &debug_report_ci, nullptr,
                                                             &m_DebugReportCallback ) );
        }
        VulkanLoadDebugUtilsExtensions( s_VulkanInstance );
        return Common::MakeSuccess( VK_SUCCESS );
    }

    void VulkanContext::BeginFrame() const
    {
        const auto window = m_Window.lock();
        if ( !window )
        {
            DESERT_VERIFY( false );
        }

        const auto& vulkanQueue = SP_CAST( VulkanSwapChain, window->GetWindowSwapChain() )->GetVulkanQueue();
        vulkanQueue->PrepareFrame();
    }

    void VulkanContext::EndFrame() const
    {
        const auto window = m_Window.lock();
        if ( !window )
        {
            DESERT_VERIFY( false );
        }

        const auto& vulkanQueue = SP_CAST( VulkanSwapChain, window->GetWindowSwapChain() )->GetVulkanQueue();
        vulkanQueue->Present();
    }

    void VulkanContext::Shutdown()
    {
        m_VulkanAllocator->Shutdown();
    }

    void VulkanContext::Init()
    {
        const auto logicalDeivce = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() );

        m_VulkanAllocator = std::make_unique<VulkanAllocator>();
        m_VulkanAllocator->Init( logicalDeivce, s_VulkanInstance );

        CommandBufferAllocator::CreateInstance( logicalDeivce );

        const auto window = m_Window.lock();
        if ( !window )
        {
            DESERT_VERIFY( false );
        }
    }

} // namespace Desert::Graphic::API::Vulkan