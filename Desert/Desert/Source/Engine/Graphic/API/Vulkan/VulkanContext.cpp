#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Core/EngineContext.h>

#include <vulkan/vulkan.h>

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

    VulkanContext::VulkanContext( GLFWwindow* window ) : m_GLFWwindow( window )
    {
    }

    Common::Result<VkResult> VulkanContext::CreateVKInstance()
    {
        LOG_TRACE( "VulkanRenderingContext::CreateVKInstance()" );
        DESERT_VERIFY( glfwVulkanSupported(), "GLFW must support Vulkan API" );
        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "Hello Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION( 1, 0, 0 );
        appInfo.pEngineName        = "No Engine";
        appInfo.engineVersion      = VK_MAKE_VERSION( 1, 0, 0 );
        appInfo.apiVersion         = VK_API_VERSION_1_0;

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
        const auto& pDevice = Graphic::API::Vulkan::VulkanPhysicalDevice::Create();
        pDevice->CreateDevice();
        auto& lDevice = Common::Singleton<VulkanLogicalDevice>::CreateInstance( pDevice );
        lDevice.CreateDevice();

        VulkanAllocator::GetInstance().Init( lDevice, s_VulkanInstance );

        CommandBufferAllocator::CreateInstance( lDevice );

        m_SwapChain = std::make_unique<VulkanSwapChain>();
        m_SwapChain->Init( m_GLFWwindow, s_VulkanInstance, lDevice );

        static uint32_t width, height;
        m_SwapChain->Create( &width, &height );

        m_VulkanQueue = std::make_unique<VulkanQueue>( m_SwapChain.get() ); // TODO: make shared ptr
        m_VulkanQueue->Init();

        VulkanRenderCommandBuffer::CreateInstance( "Main" ).Init( m_VulkanQueue.get() );
        return Common::MakeSuccess( VK_SUCCESS );
    }

    void VulkanContext::BeginFrame() const
    {
    }

    void VulkanContext::EndFrame() const
    {
    }

    void VulkanContext::PresentFinalImage() const
    {
        m_VulkanQueue->Present();
    }

} // namespace Desert::Graphic::API::Vulkan