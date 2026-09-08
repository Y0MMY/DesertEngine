#include "VulkanHost.hpp"

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace Hub
{
    namespace
    {
        // How many thumbnails plus the font atlas this launcher can hold descriptor sets for at
        // once. The grid draws one per visible project and one per template; 256 is far past any
        // plausible registry and costs a few kilobytes of pool.
        constexpr std::uint32_t kMaxTextureSets = 256;

        // Double buffering. FIFO present is what `glfwSwapInterval( 1 )` used to buy on the GL path,
        // and it is what a launcher wants: no tearing, no spinning a laptop fan on a menu.
        constexpr std::uint32_t kMinImageCount = 2;

        const char* VkResultName( VkResult result )
        {
            switch ( result )
            {
                case VK_SUCCESS:
                    return "VK_SUCCESS";
                case VK_NOT_READY:
                    return "VK_NOT_READY";
                case VK_TIMEOUT:
                    return "VK_TIMEOUT";
                case VK_SUBOPTIMAL_KHR:
                    return "VK_SUBOPTIMAL_KHR";
                case VK_ERROR_OUT_OF_HOST_MEMORY:
                    return "VK_ERROR_OUT_OF_HOST_MEMORY";
                case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                    return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
                case VK_ERROR_INITIALIZATION_FAILED:
                    return "VK_ERROR_INITIALIZATION_FAILED";
                case VK_ERROR_DEVICE_LOST:
                    return "VK_ERROR_DEVICE_LOST";
                case VK_ERROR_LAYER_NOT_PRESENT:
                    return "VK_ERROR_LAYER_NOT_PRESENT";
                case VK_ERROR_EXTENSION_NOT_PRESENT:
                    return "VK_ERROR_EXTENSION_NOT_PRESENT";
                case VK_ERROR_FEATURE_NOT_PRESENT:
                    return "VK_ERROR_FEATURE_NOT_PRESENT";
                case VK_ERROR_INCOMPATIBLE_DRIVER:
                    return "VK_ERROR_INCOMPATIBLE_DRIVER";
                case VK_ERROR_SURFACE_LOST_KHR:
                    return "VK_ERROR_SURFACE_LOST_KHR";
                case VK_ERROR_OUT_OF_DATE_KHR:
                    return "VK_ERROR_OUT_OF_DATE_KHR";
                default:
                    return "VkResult (unnamed)";
            }
        }

        // A frame loop cannot return a Result to anybody, and a launcher that silently draws nothing
        // is the failure mode this project has a rule against. So the loop's errors go to stderr —
        // ONCE per distinct message, because the alternative on a lost surface is sixty identical
        // lines a second, which is the same as saying nothing.
        void ReportOnce( const std::string& message )
        {
            static std::set<std::string> alreadySaid;
            if ( alreadySaid.insert( message ).second )
                std::fprintf( stderr, "ProjectHub/Vulkan: %s\n", message.c_str() );
        }

        void ReportCall( const char* call, VkResult result )
        {
            ReportOnce( std::string( call ) + " -> " + VkResultName( result ) );
        }

        [[nodiscard]] bool HasExtension( const std::vector<VkExtensionProperties>& available, const char* name )
        {
            for ( const VkExtensionProperties& extension : available )
                if ( std::strcmp( extension.extensionName, name ) == 0 )
                    return true;
            return false;
        }

        [[nodiscard]] std::vector<VkExtensionProperties> InstanceExtensions()
        {
            std::uint32_t count = 0;
            if ( vkEnumerateInstanceExtensionProperties( nullptr, &count, nullptr ) != VK_SUCCESS )
                return {};
            std::vector<VkExtensionProperties> out( count );
            if ( vkEnumerateInstanceExtensionProperties( nullptr, &count, out.data() ) != VK_SUCCESS )
                return {};
            return out;
        }

        [[nodiscard]] std::vector<VkExtensionProperties> DeviceExtensions( VkPhysicalDevice device )
        {
            std::uint32_t count = 0;
            if ( vkEnumerateDeviceExtensionProperties( device, nullptr, &count, nullptr ) != VK_SUCCESS )
                return {};
            std::vector<VkExtensionProperties> out( count );
            if ( vkEnumerateDeviceExtensionProperties( device, nullptr, &count, out.data() ) != VK_SUCCESS )
                return {};
            return out;
        }
    } // namespace

    // Every Vulkan handle the launcher owns. One struct rather than a dozen members on the class so
    // the header stays free of vulkan.h.
    struct VulkanHost::Device
    {
        GLFWwindow*      Glfw           = nullptr;
        VkInstance       Instance       = VK_NULL_HANDLE;
        VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
        VkDevice         Handle         = VK_NULL_HANDLE;
        std::uint32_t    QueueFamily    = 0;
        VkQueue          Queue          = VK_NULL_HANDLE;
        VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
        VkCommandPool    UploadPool     = VK_NULL_HANDLE;
        VkFence          UploadFence    = VK_NULL_HANDLE;
        // ONE sampler for every thumbnail: they are all drawn the same way (bilinear, clamped),
        // exactly as the four glTexParameteri calls this replaces said.
        VkSampler Sampler = VK_NULL_HANDLE;

        ImGui_ImplVulkanH_Window Swapchain;
        bool                     NeedsRebuild = false;

        // What a thumbnail actually is on this backend. The ThumbnailCache only ever sees the
        // descriptor set (that IS ImTextureID for imgui_impl_vulkan), so the other three handles
        // have to be findable from it.
        struct Thumbnail
        {
            VkDescriptorSet Set    = VK_NULL_HANDLE;
            VkImageView     View   = VK_NULL_HANDLE;
            VkImage         Image  = VK_NULL_HANDLE;
            VkDeviceMemory  Memory = VK_NULL_HANDLE;
        };

        struct Retiring
        {
            Thumbnail     Texture;
            std::uint64_t DiedAtFrame = 0;
        };

        std::vector<Thumbnail> Live;
        std::vector<Retiring>  Graveyard;
        std::uint64_t          FrameCounter = 0;

        [[nodiscard]] std::uint32_t MemoryType( std::uint32_t typeBits, VkMemoryPropertyFlags properties ) const
        {
            VkPhysicalDeviceMemoryProperties memory{};
            vkGetPhysicalDeviceMemoryProperties( PhysicalDevice, &memory );
            for ( std::uint32_t i = 0; i < memory.memoryTypeCount; ++i )
                if ( ( typeBits & ( 1u << i ) ) != 0 &&
                     ( memory.memoryTypes[i].propertyFlags & properties ) == properties )
                    return i;
            return ~0u; // no such heap; the caller refuses rather than binding memory it did not get
        }

        // Records `record` into a throwaway command buffer and waits for it to complete.
        //
        // A FENCE AND NOT vkQueueWaitIdle, and the difference is a stall the user would see. This
        // runs MID-FRAME (ThumbnailCache decodes inside the ImGui pass), and vkQueueWaitIdle would
        // also block on the previous frames still presenting. The fence waits for this upload only.
        // The wait itself is deliberate: the descriptor set is handed to ImGui in the same frame it
        // is created, so the copy has to be finished before that frame is recorded — and the decode
        // budget (ThumbnailCache::kDecodesPerFrame) already caps this at two per frame.
        template <typename Record>
        [[nodiscard]] bool RunOneShot( const Record& record )
        {
            VkCommandBufferAllocateInfo allocate{};
            allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate.commandPool        = UploadPool;
            allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;

            VkCommandBuffer command = VK_NULL_HANDLE;
            VkResult        result  = vkAllocateCommandBuffers( Handle, &allocate, &command );
            if ( result != VK_SUCCESS )
            {
                ReportCall( "vkAllocateCommandBuffers (upload)", result );
                return false;
            }

            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            result      = vkBeginCommandBuffer( command, &begin );
            if ( result != VK_SUCCESS )
            {
                ReportCall( "vkBeginCommandBuffer (upload)", result );
                vkFreeCommandBuffers( Handle, UploadPool, 1, &command );
                return false;
            }

            record( command );

            result = vkEndCommandBuffer( command );
            if ( result != VK_SUCCESS )
            {
                ReportCall( "vkEndCommandBuffer (upload)", result );
                vkFreeCommandBuffers( Handle, UploadPool, 1, &command );
                return false;
            }

            VkSubmitInfo submit{};
            submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers    = &command;

            vkResetFences( Handle, 1, &UploadFence );
            result = vkQueueSubmit( Queue, 1, &submit, UploadFence );
            if ( result == VK_SUCCESS )
                result = vkWaitForFences( Handle, 1, &UploadFence, VK_TRUE, UINT64_MAX );
            if ( result != VK_SUCCESS )
                ReportCall( "vkQueueSubmit/vkWaitForFences (upload)", result );

            vkFreeCommandBuffers( Handle, UploadPool, 1, &command );
            return result == VK_SUCCESS;
        }

        void DestroyThumbnail( const Thumbnail& texture )
        {
            if ( texture.Set != VK_NULL_HANDLE )
                vkFreeDescriptorSets( Handle, DescriptorPool, 1, &texture.Set );
            if ( texture.View != VK_NULL_HANDLE )
                vkDestroyImageView( Handle, texture.View, nullptr );
            if ( texture.Image != VK_NULL_HANDLE )
                vkDestroyImage( Handle, texture.Image, nullptr );
            if ( texture.Memory != VK_NULL_HANDLE )
                vkFreeMemory( Handle, texture.Memory, nullptr );
        }

        // Frees everything that has been dead for a whole swapchain's worth of frames. See the
        // comment on VulkanHost::MakeTextureBackend for why the delay exists at all.
        void RetireGraveyard()
        {
            const std::uint64_t safeAge = static_cast<std::uint64_t>( Swapchain.ImageCount ) + 1;
            for ( std::size_t i = Graveyard.size(); i-- > 0; )
            {
                if ( FrameCounter < Graveyard[i].DiedAtFrame + safeAge )
                    continue;
                DestroyThumbnail( Graveyard[i].Texture );
                Graveyard.erase( Graveyard.begin() + static_cast<std::ptrdiff_t>( i ) );
            }
        }
    };

    VulkanHost::VulkanHost()  = default;
    VulkanHost::~VulkanHost() = default;

    Common::BoolResultStr VulkanHost::Init( GLFWwindow* window )
    {
        if ( !glfwVulkanSupported() )
            // The sentence a person with a bare machine needs. On macOS this is a missing MoltenVK
            // ICD nine times out of ten, and "the launcher crashed" is what it looked like before.
            return Common::MakeError<bool>(
                 "GLFW reports no Vulkan loader. On macOS install MoltenVK (brew install molten-vk) "
                 "and make sure VK_ICD_FILENAMES points at MoltenVK_icd.json." );

        auto device  = std::make_unique<Device>();
        device->Glfw = window;

        // ---- instance -------------------------------------------------------------------------
        std::uint32_t      requiredCount = 0;
        const char* const* required      = glfwGetRequiredInstanceExtensions( &requiredCount );
        if ( required == nullptr )
            return Common::MakeError<bool>( "glfwGetRequiredInstanceExtensions returned nothing - "
                                            "no window-system surface extension is available." );

        std::vector<const char*> instanceExtensions( required, required + requiredCount );

        const std::vector<VkExtensionProperties> availableInstance = InstanceExtensions();
        VkInstanceCreateFlags                    instanceFlags     = 0;
        // MoltenVK is a PORTABILITY driver: since Vulkan loader 1.3.216 it is not enumerated at all
        // unless the instance asks for it, and vkEnumeratePhysicalDevices then returns zero devices
        // on a machine that has a perfectly good GPU. Asked for by capability rather than by
        // `#ifdef __APPLE__` so a Linux loader that grows a portability ICD gets it too.
        if ( HasExtension( availableInstance, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME ) )
        {
            instanceExtensions.push_back( VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME );
            instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            if ( HasExtension( availableInstance, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME ) )
                instanceExtensions.push_back( VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME );
        }

        VkApplicationInfo application{};
        application.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "DesertEngine Launcher";
        application.pEngineName      = "DesertEngine Launcher";
        application.apiVersion       = VK_API_VERSION_1_1;

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.flags                   = instanceFlags;
        instanceInfo.pApplicationInfo        = &application;
        instanceInfo.enabledExtensionCount   = static_cast<std::uint32_t>( instanceExtensions.size() );
        instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();

        VkResult result = vkCreateInstance( &instanceInfo, nullptr, &device->Instance );
        if ( result != VK_SUCCESS )
            return Common::MakeError<bool>( std::string( "vkCreateInstance failed: " ) + VkResultName( result ) );

        // ---- surface --------------------------------------------------------------------------
        result = glfwCreateWindowSurface( device->Instance, window, nullptr, &device->Swapchain.Surface );
        if ( result != VK_SUCCESS )
            return Common::MakeError<bool>( std::string( "glfwCreateWindowSurface failed: " ) +
                                            VkResultName( result ) );

        // ---- physical device + queue family ---------------------------------------------------
        std::uint32_t deviceCount = 0;
        result                    = vkEnumeratePhysicalDevices( device->Instance, &deviceCount, nullptr );
        if ( result != VK_SUCCESS || deviceCount == 0 )
            return Common::MakeError<bool>( "vkEnumeratePhysicalDevices found no GPU (" +
                                            std::string( VkResultName( result ) ) + ")." );
        std::vector<VkPhysicalDevice> candidates( deviceCount );
        result = vkEnumeratePhysicalDevices( device->Instance, &deviceCount, candidates.data() );
        if ( result != VK_SUCCESS )
            return Common::MakeError<bool>( std::string( "vkEnumeratePhysicalDevices failed: " ) +
                                            VkResultName( result ) );

        // A discrete GPU when there is one, otherwise whatever can draw and present. A launcher has
        // no performance reason to prefer either; it picks the one the compositor is most likely to
        // be on so the window is not composited across two devices.
        int chosenScore = -1;
        for ( VkPhysicalDevice candidate : candidates )
        {
            std::uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties( candidate, &familyCount, nullptr );
            std::vector<VkQueueFamilyProperties> families( familyCount );
            vkGetPhysicalDeviceQueueFamilyProperties( candidate, &familyCount, families.data() );

            for ( std::uint32_t family = 0; family < familyCount; ++family )
            {
                if ( ( families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT ) == 0 )
                    continue;
                VkBool32 presents = VK_FALSE;
                if ( vkGetPhysicalDeviceSurfaceSupportKHR( candidate, family, device->Swapchain.Surface,
                                                           &presents ) != VK_SUCCESS ||
                     presents != VK_TRUE )
                    continue;

                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties( candidate, &properties );
                const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
                if ( score > chosenScore )
                {
                    chosenScore            = score;
                    device->PhysicalDevice = candidate;
                    device->QueueFamily    = family;
                }
                break;
            }
        }
        if ( device->PhysicalDevice == VK_NULL_HANDLE )
            return Common::MakeError<bool>( "no Vulkan device has a queue family that can both draw and "
                                            "present to this window's surface." );

        // ---- logical device -------------------------------------------------------------------
        const std::vector<VkExtensionProperties> availableDevice = DeviceExtensions( device->PhysicalDevice );
        if ( !HasExtension( availableDevice, VK_KHR_SWAPCHAIN_EXTENSION_NAME ) )
            return Common::MakeError<bool>( "the chosen Vulkan device does not support " +
                                            std::string( VK_KHR_SWAPCHAIN_EXTENSION_NAME ) + "." );

        std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        // The spec REQUIRES this to be enabled when the implementation advertises it (MoltenVK
        // does); a device created without it is undefined behaviour that happens to work.
        if ( HasExtension( availableDevice, "VK_KHR_portability_subset" ) )
            deviceExtensions.push_back( "VK_KHR_portability_subset" );

        const float             queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = device->QueueFamily;
        queueInfo.queueCount       = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount    = 1;
        deviceInfo.pQueueCreateInfos       = &queueInfo;
        deviceInfo.enabledExtensionCount   = static_cast<std::uint32_t>( deviceExtensions.size() );
        deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

        result = vkCreateDevice( device->PhysicalDevice, &deviceInfo, nullptr, &device->Handle );
        if ( result != VK_SUCCESS )
            return Common::MakeError<bool>( std::string( "vkCreateDevice failed: " ) + VkResultName( result ) );
        vkGetDeviceQueue( device->Handle, device->QueueFamily, 0, &device->Queue );

        // ---- descriptor pool, upload pool, sampler --------------------------------------------
        VkDescriptorPoolSize poolSize{};
        poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = kMaxTextureSets;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // FREE_DESCRIPTOR_SET, and it is load-bearing: a thumbnail whose file changed is released
        // and re-uploaded while the process runs, so the sets have to come back to the pool.
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets       = kMaxTextureSets;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &poolSize;

        result = vkCreateDescriptorPool( device->Handle, &poolInfo, nullptr, &device->DescriptorPool );
        if ( result != VK_SUCCESS )
            return Common::MakeError<bool>( std::string( "vkCreateDescriptorPool failed: " ) +
                                            VkResultName( result ) );

        VkCommandPoolCreateInfo commandPoolInfo{};
        commandPoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        commandPoolInfo.queueFamilyIndex = device->QueueFamily;
        result = vkCreateCommandPool( device->Handle, &commandPoolInfo, nullptr, &device->UploadPool );
        if ( result != VK_SUCCESS )
            return Common::MakeError<bool>( std::string( "vkCreateCommandPool failed: " ) +
                                            VkResultName( result ) );

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        result          = vkCreateFence( device->Handle, &fenceInfo, nullptr, &device->UploadFence );
        if ( result != VK_SUCCESS )
            return Common::MakeError<bool>( std::string( "vkCreateFence failed: " ) + VkResultName( result ) );

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter    = VK_FILTER_LINEAR;
        samplerInfo.minFilter    = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod       = 1.0f;
        result                   = vkCreateSampler( device->Handle, &samplerInfo, nullptr, &device->Sampler );
        if ( result != VK_SUCCESS )
            return Common::MakeError<bool>( std::string( "vkCreateSampler failed: " ) + VkResultName( result ) );

        // ---- swapchain ------------------------------------------------------------------------
        //
        // UNORM FORMATS ARE REQUESTED FIRST AND THE ORDER IS THE WHOLE POINT. ImGui writes its
        // vertex colours straight through with no conversion, and the GL2 path this replaces
        // presented into a plain BGRA8 default framebuffer. Ask for an _SRGB swapchain and the
        // hardware gamma-encodes every one of those colours on write, so the entire launcher — the
        // theme, the fonts, the thumbnails — comes out visibly washed out against the same code's
        // OpenGL frame. It looks like a theme bug and it is a format choice.
        const VkFormat requestFormats[] = {
             VK_FORMAT_B8G8R8A8_UNORM,
             VK_FORMAT_R8G8B8A8_UNORM,
             VK_FORMAT_B8G8R8A8_SRGB,
             VK_FORMAT_R8G8B8A8_SRGB,
        };
        device->Swapchain.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
             device->PhysicalDevice, device->Swapchain.Surface, requestFormats,
             static_cast<int>( IM_ARRAYSIZE( requestFormats ) ), VK_COLOR_SPACE_SRGB_NONLINEAR_KHR );

        const VkPresentModeKHR requestPresentModes[] = { VK_PRESENT_MODE_FIFO_KHR };
        device->Swapchain.PresentMode                = ImGui_ImplVulkanH_SelectPresentMode(
             device->PhysicalDevice, device->Swapchain.Surface, requestPresentModes,
             static_cast<int>( IM_ARRAYSIZE( requestPresentModes ) ) );

        int framebufferWidth  = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize( window, &framebufferWidth, &framebufferHeight );
        ImGui_ImplVulkanH_CreateOrResizeWindow( device->Instance, device->PhysicalDevice, device->Handle,
                                                &device->Swapchain, device->QueueFamily, nullptr, framebufferWidth,
                                                framebufferHeight, kMinImageCount );
        if ( device->Swapchain.Swapchain == VK_NULL_HANDLE )
            return Common::MakeError<bool>( "the swapchain could not be created for this window's surface." );

        // ---- ImGui ----------------------------------------------------------------------------
        ImGui_ImplVulkan_InitInfo init{};
        init.Instance       = device->Instance;
        init.PhysicalDevice = device->PhysicalDevice;
        init.Device         = device->Handle;
        init.QueueFamily    = device->QueueFamily;
        init.Queue          = device->Queue;
        init.DescriptorPool = device->DescriptorPool;
        init.Subpass        = 0;
        init.MinImageCount  = kMinImageCount;
        init.ImageCount     = device->Swapchain.ImageCount;
        init.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
        if ( !ImGui_ImplVulkan_Init( &init, device->Swapchain.RenderPass ) )
            return Common::MakeError<bool>( "ImGui_ImplVulkan_Init refused the device." );

        // THE FONT ATLAS IS UPLOADED HERE AND NOT LAZILY, which is the one initialisation order the
        // GL2 backend hid: it built its atlas on the first NewFrame, so nothing had to know that
        // Theme::LoadFonts must run first. On Vulkan it does, and a caller that loads a font after
        // this line gets a window with no text in it.
        bool fontsUploaded = false;
        if ( device->RunOneShot( [&fontsUploaded]( VkCommandBuffer command )
                                 { fontsUploaded = ImGui_ImplVulkan_CreateFontsTexture( command ); } ) &&
             fontsUploaded )
            ImGui_ImplVulkan_DestroyFontUploadObjects();
        else
            return Common::MakeError<bool>( "the ImGui font atlas could not be uploaded." );

        m_Device = std::move( device );
        return Common::MakeSuccess( true );
    }

    void VulkanHost::BeginFrame()
    {
        if ( !m_Device )
            return;

        int framebufferWidth  = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize( m_Device->Glfw, &framebufferWidth, &framebufferHeight );
        if ( framebufferWidth <= 0 || framebufferHeight <= 0 )
            return; // minimised: nothing to resize to, and the frame is dropped in Present anyway

        if ( m_Device->NeedsRebuild || framebufferWidth != m_Device->Swapchain.Width ||
             framebufferHeight != m_Device->Swapchain.Height )
        {
            ImGui_ImplVulkan_SetMinImageCount( kMinImageCount );
            ImGui_ImplVulkanH_CreateOrResizeWindow( m_Device->Instance, m_Device->PhysicalDevice, m_Device->Handle,
                                                    &m_Device->Swapchain, m_Device->QueueFamily, nullptr,
                                                    framebufferWidth, framebufferHeight, kMinImageCount );
            m_Device->Swapchain.FrameIndex = 0;
            m_Device->NeedsRebuild         = false;
        }

        ImGui_ImplVulkan_NewFrame();
    }

    void VulkanHost::Present( ImDrawData* drawData, const float clear[4] )
    {
        if ( !m_Device || drawData == nullptr )
            return;
        if ( drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f )
            return; // minimised
        if ( m_Device->NeedsRebuild )
            return; // the swapchain is stale; BeginFrame rebuilds it and the next frame draws

        ImGui_ImplVulkanH_Window& window   = m_Device->Swapchain;
        window.ClearValue.color.float32[0] = clear[0];
        window.ClearValue.color.float32[1] = clear[1];
        window.ClearValue.color.float32[2] = clear[2];
        window.ClearValue.color.float32[3] = clear[3];

        VkSemaphore imageAcquired = window.FrameSemaphores[window.SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore renderDone    = window.FrameSemaphores[window.SemaphoreIndex].RenderCompleteSemaphore;

        VkResult result = vkAcquireNextImageKHR( m_Device->Handle, window.Swapchain, UINT64_MAX, imageAcquired,
                                                 VK_NULL_HANDLE, &window.FrameIndex );
        if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR )
        {
            m_Device->NeedsRebuild = true;
            return;
        }
        if ( result != VK_SUCCESS )
        {
            ReportCall( "vkAcquireNextImageKHR", result );
            return;
        }

        ImGui_ImplVulkanH_Frame& frame = window.Frames[window.FrameIndex];

        result = vkWaitForFences( m_Device->Handle, 1, &frame.Fence, VK_TRUE, UINT64_MAX );
        if ( result == VK_SUCCESS )
            result = vkResetFences( m_Device->Handle, 1, &frame.Fence );
        if ( result != VK_SUCCESS )
        {
            ReportCall( "vkWaitForFences/vkResetFences (frame)", result );
            return;
        }

        result = vkResetCommandPool( m_Device->Handle, frame.CommandPool, 0 );
        if ( result != VK_SUCCESS )
        {
            ReportCall( "vkResetCommandPool", result );
            return;
        }

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result      = vkBeginCommandBuffer( frame.CommandBuffer, &begin );
        if ( result != VK_SUCCESS )
        {
            ReportCall( "vkBeginCommandBuffer (frame)", result );
            return;
        }

        VkRenderPassBeginInfo pass{};
        pass.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass               = window.RenderPass;
        pass.framebuffer              = frame.Framebuffer;
        pass.renderArea.extent.width  = static_cast<std::uint32_t>( window.Width );
        pass.renderArea.extent.height = static_cast<std::uint32_t>( window.Height );
        pass.clearValueCount          = 1;
        pass.pClearValues             = &window.ClearValue;
        vkCmdBeginRenderPass( frame.CommandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE );

        ImGui_ImplVulkan_RenderDrawData( drawData, frame.CommandBuffer );

        vkCmdEndRenderPass( frame.CommandBuffer );
        result = vkEndCommandBuffer( frame.CommandBuffer );
        if ( result != VK_SUCCESS )
        {
            ReportCall( "vkEndCommandBuffer (frame)", result );
            return;
        }

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit{};
        submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &imageAcquired;
        submit.pWaitDstStageMask    = &waitStage;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &frame.CommandBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &renderDone;

        result = vkQueueSubmit( m_Device->Queue, 1, &submit, frame.Fence );
        if ( result != VK_SUCCESS )
        {
            ReportCall( "vkQueueSubmit (frame)", result );
            return;
        }

        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &renderDone;
        present.swapchainCount     = 1;
        present.pSwapchains        = &window.Swapchain;
        present.pImageIndices      = &window.FrameIndex;

        result = vkQueuePresentKHR( m_Device->Queue, &present );
        if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR )
            m_Device->NeedsRebuild = true;
        else if ( result != VK_SUCCESS )
            ReportCall( "vkQueuePresentKHR", result );

        window.SemaphoreIndex = ( window.SemaphoreIndex + 1 ) % window.ImageCount;

        ++m_Device->FrameCounter;
        m_Device->RetireGraveyard();
    }

    void VulkanHost::Shutdown()
    {
        if ( !m_Device )
            return;

        if ( m_Device->Handle != VK_NULL_HANDLE )
            vkDeviceWaitIdle( m_Device->Handle );

        // The device is idle, so everything on the graveyard is releasable now regardless of age —
        // and it is not empty: ThumbnailCache::Shutdown runs just before this and puts every live
        // thumbnail on it.
        for ( const Device::Retiring& dead : m_Device->Graveyard )
            m_Device->DestroyThumbnail( dead.Texture );
        m_Device->Graveyard.clear();
        for ( const Device::Thumbnail& live : m_Device->Live )
            m_Device->DestroyThumbnail( live );
        m_Device->Live.clear();

        ImGui_ImplVulkan_Shutdown();

        if ( m_Device->Sampler != VK_NULL_HANDLE )
            vkDestroySampler( m_Device->Handle, m_Device->Sampler, nullptr );
        if ( m_Device->UploadFence != VK_NULL_HANDLE )
            vkDestroyFence( m_Device->Handle, m_Device->UploadFence, nullptr );
        if ( m_Device->UploadPool != VK_NULL_HANDLE )
            vkDestroyCommandPool( m_Device->Handle, m_Device->UploadPool, nullptr );
        if ( m_Device->DescriptorPool != VK_NULL_HANDLE )
            vkDestroyDescriptorPool( m_Device->Handle, m_Device->DescriptorPool, nullptr );

        ImGui_ImplVulkanH_DestroyWindow( m_Device->Instance, m_Device->Handle, &m_Device->Swapchain, nullptr );

        if ( m_Device->Handle != VK_NULL_HANDLE )
            vkDestroyDevice( m_Device->Handle, nullptr );
        if ( m_Device->Instance != VK_NULL_HANDLE )
            vkDestroyInstance( m_Device->Instance, nullptr );

        m_Device.reset();
    }

    TextureBackend VulkanHost::MakeTextureBackend()
    {
        TextureBackend backend;

        backend.Upload = [this]( const std::uint8_t* rgba, int width, int height ) -> TextureHandle
        {
            if ( !m_Device || rgba == nullptr || width <= 0 || height <= 0 )
                return nullptr;
            Device& device = *m_Device;

            const VkDeviceSize bytes =
                 static_cast<VkDeviceSize>( width ) * static_cast<VkDeviceSize>( height ) * 4u;

            Device::Thumbnail texture;

            VkImageCreateInfo imageInfo{};
            imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType     = VK_IMAGE_TYPE_2D;
            imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.extent.width  = static_cast<std::uint32_t>( width );
            imageInfo.extent.height = static_cast<std::uint32_t>( height );
            imageInfo.extent.depth  = 1;
            imageInfo.mipLevels     = 1;
            imageInfo.arrayLayers   = 1;
            imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if ( vkCreateImage( device.Handle, &imageInfo, nullptr, &texture.Image ) != VK_SUCCESS )
            {
                ReportOnce( "vkCreateImage failed for a thumbnail" );
                return nullptr;
            }

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements( device.Handle, texture.Image, &requirements );
            VkMemoryAllocateInfo allocate{};
            allocate.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocate.allocationSize = requirements.size;
            allocate.memoryTypeIndex =
                 device.MemoryType( requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
            if ( allocate.memoryTypeIndex == ~0u ||
                 vkAllocateMemory( device.Handle, &allocate, nullptr, &texture.Memory ) != VK_SUCCESS ||
                 vkBindImageMemory( device.Handle, texture.Image, texture.Memory, 0 ) != VK_SUCCESS )
            {
                ReportOnce( "no device-local memory for a thumbnail image" );
                device.DestroyThumbnail( texture );
                return nullptr;
            }

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image                       = texture.Image;
            viewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format                      = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            if ( vkCreateImageView( device.Handle, &viewInfo, nullptr, &texture.View ) != VK_SUCCESS )
            {
                ReportOnce( "vkCreateImageView failed for a thumbnail" );
                device.DestroyThumbnail( texture );
                return nullptr;
            }

            // ---- staging copy ----
            VkBuffer       staging       = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size        = bytes;
            bufferInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if ( vkCreateBuffer( device.Handle, &bufferInfo, nullptr, &staging ) != VK_SUCCESS )
            {
                ReportOnce( "vkCreateBuffer failed for a thumbnail staging copy" );
                device.DestroyThumbnail( texture );
                return nullptr;
            }

            vkGetBufferMemoryRequirements( device.Handle, staging, &requirements );
            allocate.allocationSize = requirements.size;
            allocate.memoryTypeIndex =
                 device.MemoryType( requirements.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
            void* mapped = nullptr;
            if ( allocate.memoryTypeIndex == ~0u ||
                 vkAllocateMemory( device.Handle, &allocate, nullptr, &stagingMemory ) != VK_SUCCESS ||
                 vkBindBufferMemory( device.Handle, staging, stagingMemory, 0 ) != VK_SUCCESS ||
                 vkMapMemory( device.Handle, stagingMemory, 0, bytes, 0, &mapped ) != VK_SUCCESS )
            {
                ReportOnce( "no host-visible memory for a thumbnail staging copy" );
                if ( stagingMemory != VK_NULL_HANDLE )
                    vkFreeMemory( device.Handle, stagingMemory, nullptr );
                vkDestroyBuffer( device.Handle, staging, nullptr );
                device.DestroyThumbnail( texture );
                return nullptr;
            }
            std::memcpy( mapped, rgba, static_cast<std::size_t>( bytes ) );
            vkUnmapMemory( device.Handle, stagingMemory );

            VkImage    image  = texture.Image;
            const bool copied = device.RunOneShot(
                 [image, staging, width, height]( VkCommandBuffer command )
                 {
                     VkImageMemoryBarrier toTransfer{};
                     toTransfer.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                     toTransfer.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
                     toTransfer.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                     toTransfer.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
                     toTransfer.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
                     toTransfer.image                       = image;
                     toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                     toTransfer.subresourceRange.levelCount = 1;
                     toTransfer.subresourceRange.layerCount = 1;
                     toTransfer.dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
                     vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                           &toTransfer );

                     VkBufferImageCopy region{};
                     region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                     region.imageSubresource.layerCount = 1;
                     region.imageExtent.width           = static_cast<std::uint32_t>( width );
                     region.imageExtent.height          = static_cast<std::uint32_t>( height );
                     region.imageExtent.depth           = 1;
                     vkCmdCopyBufferToImage( command, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                             &region );

                     VkImageMemoryBarrier toShader = toTransfer;
                     toShader.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                     toShader.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                     toShader.srcAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
                     toShader.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
                     vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                           &toShader );
                 } );

            vkFreeMemory( device.Handle, stagingMemory, nullptr );
            vkDestroyBuffer( device.Handle, staging, nullptr );

            if ( !copied )
            {
                device.DestroyThumbnail( texture );
                return nullptr;
            }

            texture.Set = ImGui_ImplVulkan_AddTexture( device.Sampler, texture.View,
                                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
            if ( texture.Set == VK_NULL_HANDLE )
            {
                // The pool is full. Said out loud: the tiles would otherwise just stop having
                // pictures, which reads as a decode failure rather than a budget.
                ReportOnce( "the descriptor pool is full - no more thumbnails can be shown" );
                device.DestroyThumbnail( texture );
                return nullptr;
            }

            device.Live.push_back( texture );
            return reinterpret_cast<TextureHandle>( texture.Set );
        };

        backend.Destroy = [this]( TextureHandle handle )
        {
            if ( !m_Device || handle == nullptr )
                return;
            Device&               device = *m_Device;
            const VkDescriptorSet set    = reinterpret_cast<VkDescriptorSet>( handle );

            const auto found =
                 std::find_if( device.Live.begin(), device.Live.end(),
                               [set]( const Device::Thumbnail& candidate ) { return candidate.Set == set; } );
            if ( found == device.Live.end() )
            {
                ReportOnce( "a thumbnail was released that this backend never handed out" );
                return;
            }

            Device::Retiring retiring;
            retiring.Texture     = *found;
            retiring.DiedAtFrame = device.FrameCounter;
            device.Graveyard.push_back( retiring );
            device.Live.erase( found );
        };

        return backend;
    }
} // namespace Hub
