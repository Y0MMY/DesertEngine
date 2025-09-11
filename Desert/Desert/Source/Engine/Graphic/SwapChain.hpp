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
        virtual Common::Result<bool> CreateSwapChain( const std::shared_ptr<Engine::Device>& device,
                                                      uint32_t* width, uint32_t* height ) = 0;

        virtual uint32_t GetBackBufferCount() const = 0;

        virtual uint32_t GetWidth() const                            = 0;
        virtual uint32_t GetHeight() const                           = 0;
        virtual void     OnResize( uint32_t width, uint32_t height ) = 0;

        virtual void Release() = 0;

        static std::shared_ptr<SwapChain> Create( const GLFWwindow* window );

    protected:
        const GLFWwindow* m_Window;
    };
} // namespace Desert::Graphic