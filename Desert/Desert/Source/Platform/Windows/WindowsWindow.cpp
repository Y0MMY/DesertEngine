#include <Platform/Windows/WindowsWindow.hpp>

#include <Common/Core/Events/WindowEvents.hpp>
#include <Common/Core/Events/MouseEvents.hpp>
#include <Common/Core/Events/KeyEvents.hpp>
#include <Common/Core/Events/WindowEvents.hpp>

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Platform::Windows
{

    static void GLFWErrorCallback( int error, const char* description )
    {
        LOG_ERROR( "GLFW Error: ({}: {})", error, description );
    }

    static bool s_GLFWInitialized = false;

    Common::ResultStr<bool> WindowsWindow::Init()
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

        auto width  = m_Data.Specification.Width;
        auto height = m_Data.Specification.Height;

        GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode    = glfwGetVideoMode( monitor );
        if ( m_Data.Specification.Fullscreen )
        {

            width  = mode->width;
            height = mode->height;

            m_Data.Specification.Width  = width;
            m_Data.Specification.Height = height;
        }

        m_GLFWWindow =
             glfwCreateWindow( (int)width, (int)height, m_Data.Specification.Title.c_str(), nullptr, nullptr );

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

        glfwSetWindowSizeCallback( m_GLFWWindow,
                                   []( GLFWwindow* window, int width, int height )
                                   {
                                       auto& data = *( (WindowData*)glfwGetWindowUserPointer( window ) );

                                       Common::EventWindowResize event( (uint32_t)width, (uint32_t)height );
                                       data.EventCallback( event );
                                       data.Specification.Width  = width;
                                       data.Specification.Height = height;
                                   } );

        glfwSetKeyCallback( m_GLFWWindow,
                            []( GLFWwindow* window, int key, int scancode, int action, int mods )
                            {
                                auto& data = *(WindowData*)glfwGetWindowUserPointer( window );

                                switch ( action )
                                {
                                    case GLFW_PRESS:
                                    {
                                        Common::KeyPressedEvent event( (Common::KeyCode)key, 0 );
                                        data.EventCallback( event );
                                        break;
                                    }
                                    case GLFW_REPEAT:
                                    {
                                        Common::KeyPressedEvent event( (Common::KeyCode)key, 1 );
                                        data.EventCallback( event );
                                        break;
                                    }
                                }
                            } );

        glfwSetMouseButtonCallback( m_GLFWWindow,
                                    []( GLFWwindow* window, int button, int action, int mods )
                                    {
                                        auto& data = *( (WindowData*)glfwGetWindowUserPointer( window ) );

                                        switch ( action )
                                        {
                                            case GLFW_PRESS:
                                            {
                                                Common::MouseButtonPressedEvent event(
                                                     (Common::MouseButton)button );
                                                data.EventCallback( event );
                                                break;
                                            }
                                                /* case GLFW_RELEASE:
                                                 {
                                                     Common::MouseButtonReleasedEvent event( button );
                                                     data.EventCallback( event );
                                                     break;
                                                 }*/
                                        }
                                    } );

        m_SwapChain = Graphic::SwapChain::Create( m_GLFWWindow );

        return Common::MakeSuccess( true );
    }

    WindowsWindow::WindowsWindow( const WindowSpecification& specification )
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
        return glfwGetWindowMonitor( m_GLFWWindow ) != nullptr;
    }

    bool WindowsWindow::IsWindowMinimized() const
    {
        return false;
    }

    void WindowsWindow::Maximize()
    {
        GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode    = glfwGetVideoMode( monitor );

        m_Data.Specification.Width  = mode->width;
        m_Data.Specification.Height = mode->height;

        glfwSetWindowMonitor( m_GLFWWindow, monitor, 0, 0, mode->width, mode->height, mode->refreshRate );
    }

    void WindowsWindow::ProcessEvents()
    {
        glfwPollEvents();
    }

    void WindowsWindow::PresentFinalImage() const
    {
        EngineContext::GetInstance().GetRendererContext()->EndFrame();
    }

    void WindowsWindow::PrepareNextFrame() const
    {
        EngineContext::GetInstance().GetRendererContext()->BeginFrame();
    }

    void WindowsWindow::SetTitle( const std::string& title )
    {
        m_Data.Specification.Title = title;
        glfwSetWindowTitle( m_GLFWWindow, title.c_str() );
    }

    void WindowsWindow::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowResize>( [this]( Common::EventWindowResize& e )
                                                        { return this->OnEventWindowResize( e ); } );
    }

    bool WindowsWindow::OnEventWindowResize( Common::EventWindowResize& e )
    {
        m_SwapChain->OnResize( e.width, e.height );

        return false;
    }

    Common::ResultStr<bool> WindowsWindow::SetupSwapChain()
    {
        const auto device = EngineContext::GetInstance().GetMainDevice();
        return m_SwapChain->CreateSwapChain( device, &m_Data.Specification.Width, &m_Data.Specification.Height );
    }

    WindowsWindow::~WindowsWindow()
    {
    }

} // namespace Desert::Platform::Windows