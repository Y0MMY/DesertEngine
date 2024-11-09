#pragma once

#include <Common/Core/Singleton.hpp>
#include <Common/Core/Window.hpp>
#include <Engine/Core/Application.hpp>

#include <GLFW/glfw3.h>

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

    private:
        std::shared_ptr<Common::Window> m_CurrentWindow;

    private:
        friend class Desert::Engine::Application;
    };
} // namespace Desert