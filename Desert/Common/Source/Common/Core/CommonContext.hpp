#pragma once

#include <GLFW/glfw3.h>
#include <memory>
#include "Window.hpp"
#include "Singleton.hpp"

namespace Common
{
    class CommonContext final : public Singleton<CommonContext>
    {
    public:
        void Initialize( const std::shared_ptr<Window>& window )
        {
            m_CurrentWindow = window;
        }

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
            if ( auto window = m_CurrentWindow.lock() ) [[likely]]
            {
                return (GLFWwindow*)window->GetNativeWindow();
            }
            else
            {
                return nullptr;
            }
        }

    private:
        std::weak_ptr<Common::Window> m_CurrentWindow;
    };
} // namespace Common