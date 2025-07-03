#pragma once

#include <Common/Core/Singleton.hpp>
#include <Common/Core/Window.hpp>
#include <Engine/Core/Application.hpp>

#include <GLFW/glfw3.h>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanQueue;
    class VulkanSwapChain;
} // namespace Desert::Graphic::API::Vulkan

namespace Desert
{
    class EngineContext final : public Common::Singleton<EngineContext>
    {
    public:
        uint32_t GetCurrentWindowWidth()
        {
            if ( auto window = m_CurrentWindow.lock() ) [[likely]]
            {
                return window->GetWidth();
            }
            else [[unlikely]]
            {
                return ~0;
            }
        }

        uint32_t GetCurrentWindowHeight()
        {
            if ( auto window = m_CurrentWindow.lock() ) [[likely]]
            {
                return window->GetHeight();
            }
            else
            {
                return ~0;
            }
        }

        GLFWwindow* GetCurrentPointerToGLFWwinodw()
        {
            if (auto window = m_CurrentWindow.lock()) [[likely]]
            {
                return (GLFWwindow*)window->GetNativeWindow();
            }
            else
            {
                return nullptr;
            }
        }

        uint32_t GetCurrentFrameIndex() const
        {
            return m_CurrentFrameIndex;
        }

        uint32_t GetPreviousFrameIndex() const
        {
            return m_PrevioustBufferIndex;
        }

        uint32_t GetFramesInFlight() const
        {
            return m_FramesInFlight;
        }

    private:
        std::weak_ptr<Common::Window> m_CurrentWindow;

        uint32_t m_CurrentFrameIndex    = 0;
        uint32_t m_FramesInFlight       = 2;
        uint32_t m_PrevioustBufferIndex = m_FramesInFlight;

    private:
        friend class Desert::Engine::Application;
        friend class Desert::Graphic::API::Vulkan::VulkanQueue;
        friend class Desert::Graphic::API::Vulkan::VulkanSwapChain;
    };
} // namespace Desert