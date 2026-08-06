#pragma once

#include <GLFW/glfw3.h>

#include <Engine/Core/Device.hpp>

namespace Desert::Graphic
{
    class SwapChain
    {
    public:
        SwapChain( const GLFWwindow* window ) : m_Window( window )
        {
        }
        virtual ~SwapChain() = default;

        // TODO: Custom result value like VkResult
        virtual Common::ResultStr<bool> CreateSwapChain( const std::shared_ptr<Engine::Device>& device,
                                                      uint32_t* width, uint32_t* height ) = 0;

        virtual uint32_t GetBackBufferCount() const = 0;

        virtual uint32_t GetWidth() const                            = 0;
        virtual uint32_t GetHeight() const                           = 0;
        virtual void     OnResize( uint32_t width, uint32_t height ) = 0;

        virtual void Release() = 0;

        // Present pacing. ON = sync to the display (no tearing, frame rate capped at the refresh rate of
        // the monitor the window is on); OFF = present as fast as the GPU finishes, which is what an
        // uncapped FPS reading needs. Takes effect the next time the swapchain is (re)created — callers
        // that toggle it at runtime must trigger a recreate, which OnResize already does.
        void SetVSync( bool enabled )
        {
            m_VSync = enabled;
        }
        bool IsVSyncEnabled() const
        {
            return m_VSync;
        }

        static std::shared_ptr<SwapChain> Create( const GLFWwindow* window );

    protected:
        const GLFWwindow* m_Window;
        bool              m_VSync = true;
    };
} // namespace Desert::Graphic