#include <Platform/Windows/WindowsWindow.hpp>

#include <Common/Core/Events/WindowEvents.hpp>

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Core/EngineContext.h>

namespace Desert::Platform::Windows
{

    static void GLFWErrorCallback( int error, const char* description )
    {
        LOG_ERROR( "GLFW Error: ({}: {})", error, description );
    }

    static bool s_GLFWInitialized = false;

    Common::Result<bool> WindowsWindow::Init()
    {
        if ( !s_GLFWInitialized )
        {
            // TODO: glfwTerminate on system shutdown
            int success = glfwInit();
            if ( !success )
            {
                return Common::MakeError<bool>( "Could not intialize GLFW!" );
            }

            glfwSetErrorCallback( GLFWErrorCallback );
            s_GLFWInitialized = true;
        }

        if ( Graphic::RendererAPI::GetAPIType() == Graphic::RendererAPIType::Vulkan )
            glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );

        m_GLFWWindow = glfwCreateWindow( (int)m_Data.Specification.Width, (int)m_Data.Specification.Height,
                                         m_Data.Specification.Title.c_str(), nullptr, nullptr );

       // EngineContext::GetInstance().m_CurrentWindow = m_GLFWWindow;

        LOG_INFO( "The Window (windows) was created with: Title = {}, Width = {}, Height = {}",
                  m_Data.Specification.Title.c_str(), m_Data.Specification.Width, m_Data.Specification.Height );

        glfwSetWindowUserPointer( m_GLFWWindow, &m_Data );

        glfwSetWindowCloseCallback( m_GLFWWindow,
                                    []( GLFWwindow* window )
                                    {
                                        auto& data = *(WindowData*)glfwGetWindowUserPointer( window );

                                        Common::EventWindowClose event;
                                        data.EventCallback( event );
                                    } );

        return Common::MakeSuccess( true );
    }

    WindowsWindow::WindowsWindow( const Common::WindowSpecification& specification )
    {
        m_Data.Specification = specification;
    }

    void WindowsWindow::SetWindowSize( uint32_t width, uint32_t height )
    {
        m_Data.Specification.Width  = width;
        m_Data.Specification.Height = height;
    }

    uint32_t WindowsWindow::GetWidth() const
    {
        return m_Data.Specification.Width;
    }

    uint32_t WindowsWindow::GetHeight() const
    {
        return m_Data.Specification.Height;
    }

    const void* WindowsWindow::GetNativeWindow() const
    {
        return m_GLFWWindow;
    }

    bool WindowsWindow::IsWindowMaximized() const
    {

        return false;
    }

    bool WindowsWindow::IsWindowMinimized() const
    {
        return false;
    }

    void WindowsWindow::Maximize() const
    {
    }

    void WindowsWindow::ProcessEvents()
    {
        glfwPollEvents();
    }

} // namespace Desert::Platform::Windows