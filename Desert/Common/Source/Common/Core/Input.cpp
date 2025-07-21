#include <Common/Core/Input.hpp>
#include <Common/Core/CommonContext.hpp>

#include <GLFW/glfw3.h>

namespace Common::Input
{
    bool Keyboard::IsKeyPressed( KeyCode key )
    {
        auto state = glfwGetKey( (GLFWwindow*)CommonContext::GetInstance().GetCurrentPointerToGLFWwinodw(),
                                 static_cast<int32_t>( key ) );

        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    Mouse::Mouse() : m_Window( CommonContext::GetInstance().GetCurrentPointerToGLFWwinodw() )
    {
    }

    bool Mouse::IsMouseButtonPressed( MouseButton button )
    {
        auto state = glfwGetMouseButton( (GLFWwindow*)m_Window, static_cast<int32_t>( button ) );

        return state == GLFW_PRESS;
    }

    float Mouse::GetMouseX()
    {
        auto [x, y] = GetMousePosition();
        return (float)x;
    }

    float Mouse::GetMouseY()
    {
        auto [x, y] = GetMousePosition();
        return (float)y;
    }

    std::pair<float, float> Mouse::GetMousePosition()
    {
        double x, y;
        glfwGetCursorPos( (GLFWwindow*)m_Window, &x, &y );
        return { (float)x, (float)y };
    }

    Mouse& Mouse::Get()
    {
        static Mouse s_Instance;
        return s_Instance;
    }

    void Mouse::SetCursorMode( MouseState mode )
    {
        m_MouseMode = mode;

        glfwSetInputMode( (GLFWwindow*)( m_Window ), GLFW_CURSOR, GLFW_CURSOR_NORMAL + (int)mode );
    }

} // namespace Common::Input