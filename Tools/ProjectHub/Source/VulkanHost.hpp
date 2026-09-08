#pragma once

// The launcher's own, minimal Vulkan device — instance, physical device, queue, surface, swapchain,
// and the descriptor pool ImGui draws through. EVERY graphics-API call this program makes is in
// VulkanHost.cpp; Main.cpp names none, and neither does any screen.
//
// WHY THE LAUNCHER HAS ITS OWN AND DOES NOT SHARE THE ENGINE'S. The launcher links no engine code
// (R1) and after L3 does not even live in the same repository, so `Engine::VulkanContext` is not
// reachable and copying it would be worse than not: it carries a swapchain that renders a scene,
// a render graph, a device-lost latch, a shader cache and a frame-state registry, none of which a
// window that draws one ImGui pass can use. What is actually needed to present ImGui is the list
// below, and Dear ImGui already ships the swapchain half of it as `ImGui_ImplVulkanH_Window`. So
// this file is the ~400 lines the engine's ~4000 would have had to be cut down to anyway.
//
// WHAT IS DELIBERATELY NOT HERE: no validation layers (a launcher is not a place to develop
// shaders — it draws one vertex buffer produced by upstream code that the engine's own validated
// runs already exercise), no MSAA, no depth buffer, no second queue. Adding any of them is a
// change to this file and to nothing else.

#include <DesertShared/ResultStr.hpp>

#include "Thumbnails.hpp"

#include <memory>

struct GLFWwindow;
struct ImDrawData;

namespace Hub
{
    class VulkanHost
    {
    public:
        VulkanHost();
        ~VulkanHost();

        // `window` must have been created with GLFW_CLIENT_API = GLFW_NO_API. Failure is returned
        // with the call that failed and its VkResult named: a launcher that cannot reach a GPU has
        // to say which step refused, because on macOS the answer is almost always a missing
        // MoltenVK ICD rather than a broken program.
        [[nodiscard]] Common::BoolResultStr Init( GLFWwindow* window );

        // Between Init and Shutdown, once per frame, BEFORE ImGui::NewFrame. Rebuilds the swapchain
        // if the window was resized since the last frame.
        void BeginFrame();

        // Records and presents `drawData` over a clear to `clear` (RGBA, straight, 0..1). A frame
        // whose swapchain went out of date is DROPPED rather than half-presented; the next
        // BeginFrame rebuilds. Nothing is returned because there is nothing a caller could do.
        void Present( ImDrawData* drawData, const float clear[4] );

        // Idles the device and destroys everything, in reverse. Must be called while the window
        // still exists.
        void Shutdown();

        // The thumbnail cache's two callbacks, bound to this device.
        //
        // THE DESTROY CALLBACK IS DEFERRED, AND THAT IS THE ONE PLACE THE OPENGL CONTRACT DID NOT
        // SURVIVE THE PORT. ThumbnailCache::Get destroys a texture MID-FRAME when the file under it
        // changed, and the frames still in flight are holding descriptor sets that point at it. GL
        // let that pass; Vulkan does not. So a destroyed thumbnail goes on a graveyard tagged with
        // the frame it died in and is freed once as many frames have been presented as the
        // swapchain has images — by which point the fence for the frame that used it has been
        // waited on.
        [[nodiscard]] TextureBackend MakeTextureBackend();

    private:
        struct Device;
        // The Vulkan handles live behind this so no consumer of this header parses vulkan.h — which
        // is what keeps `MakeTextureBackend`'s return type (Thumbnails.hpp) free of one too.
        std::unique_ptr<Device> m_Device;
    };
} // namespace Hub
