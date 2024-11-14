#pragma once

#include <Common/Core/Singleton.hpp>
#include <Common/Core/Window.hpp>
#include <Engine/Core/Application.hpp>

#include <GLFW/glfw3.h>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanQueue;
}

namespace Desert
{
    class EngineContext final : public Common::Singleton<EngineContext>
    {
    public:
        uint32_t GetCurrentWindowWidth()
        {
            return m_CurrentWindow->GetWidth();
        }
        uint32_t GetCurrentWindowHeight()
        {
            return m_CurrentWindow->GetHeight();
        }

        GLFWwindow* GetCurrentPointerToGLFWwinodw()
        {
            return (GLFWwindow*)m_CurrentWindow->GetNativeWindow();
        }

        uint32_t GetCurrentBufferIndex() const
        {
            return m_CurrentBufferIndex;
        }

    private:
        std::shared_ptr<Common::Window> m_CurrentWindow;

        uint32_t m_CurrentBufferIndex = 0;

    private:
        friend class Desert::Engine::Application;
        friend class Desert::Graphic::API::Vulkan::VulkanQueue;
    };
} // namespace Desert