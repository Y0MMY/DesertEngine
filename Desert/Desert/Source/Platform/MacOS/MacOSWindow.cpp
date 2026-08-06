#include <Platform/MacOS/MacOSWindow.hpp>

#include <Common/Core/Events/WindowEvents.hpp>
#include <Common/Core/Events/MouseEvents.hpp>
#include <Common/Core/Events/KeyEvents.hpp>

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Platform::MacOS
{

    static void GLFWErrorCallback( int error, const char* description )
    {
        LOG_ERROR( "GLFW Error: ({}: {})", error, description );
    }

    static bool s_GLFWInitialized = false;

    Common::ResultStr<bool> MacOSWindow::Init()
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

        GLFWmonitor*       monitor      = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode         = glfwGetVideoMode( monitor );
        int                posX         = 0, posY = 0;
        bool               setPos       = false;
        const bool         coverTaskbar = m_Data.Specification.Fullscreen && m_Data.Specification.FullscreenCoverTaskbar;

        if ( m_Data.Specification.Fullscreen )
        {
            if ( coverTaskbar )
            {
                // Borderless over the whole monitor (no decorations, covers the Dock/menu bar area).
                glfwWindowHint( GLFW_DECORATED, GLFW_FALSE );
                width  = mode->width;
                height = mode->height;
                posX   = 0;
                posY   = 0;
                setPos = true;
            }
            else
            {
                // Decorated window MAXIMIZED to the work area: keeps the title bar and leaves the
                // menu bar/Dock visible. The OS positions/sizes it; we just give a sane restore size.
                glfwWindowHint( GLFW_DECORATED, GLFW_TRUE );
                glfwWindowHint( GLFW_MAXIMIZED, GLFW_TRUE );
                int wx, wy, ww, wh;
                glfwGetMonitorWorkarea( monitor, &wx, &wy, &ww, &wh );
                width  = (uint32_t)ww;
                height = (uint32_t)wh;
            }

            m_Data.Specification.Width  = width;
            m_Data.Specification.Height = height;
        }

        m_GLFWWindow =
             glfwCreateWindow( (int)width, (int)height, m_Data.Specification.Title.c_str(), nullptr, nullptr );

        if ( setPos && m_GLFWWindow )
            glfwSetWindowPos( m_GLFWWindow, posX, posY );

        // Maximized open size differs from the restore size -> sync the spec to the real client size so the
        // swapchain/camera use the correct dimensions.
        if ( m_Data.Specification.Fullscreen && !coverTaskbar && m_GLFWWindow )
        {
            int fw = 0, fh = 0;
            glfwGetWindowSize( m_GLFWWindow, &fw, &fh );
            if ( fw > 0 && fh > 0 )
            {
                m_Data.Specification.Width  = (uint32_t)fw;
                m_Data.Specification.Height = (uint32_t)fh;
            }
        }

        glfwWindowHint( GLFW_MAXIMIZED, GLFW_FALSE ); // reset sticky hint

        LOG_INFO( "The Window (macOS) was created with: Title = {}, Width = {}, Height = {}",
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

        glfwSetScrollCallback( m_GLFWWindow,
                               []( GLFWwindow* window, double xOffset, double yOffset )
                               {
                                   auto& data = *( (WindowData*)glfwGetWindowUserPointer( window ) );
                                   Common::MouseScrolledEvent event( (float)xOffset, (float)yOffset );
                                   data.EventCallback( event );
                               } );

        glfwSetCharCallback( m_GLFWWindow,
                             []( GLFWwindow* window, unsigned int codepoint )
                             {
                                 auto&                 data = *( (WindowData*)glfwGetWindowUserPointer( window ) );
                                 Common::KeyTypedEvent event( codepoint );
                                 data.EventCallback( event );
                             } );

        glfwSetDropCallback( m_GLFWWindow,
                             []( GLFWwindow* window, int count, const char** paths )
                             {
                                 auto& data = *( (WindowData*)glfwGetWindowUserPointer( window ) );

                                 std::vector<std::string> dropped;
                                 dropped.reserve( count );
                                 for ( int i = 0; i < count; ++i )
                                     dropped.emplace_back( paths[i] );

                                 Common::EventWindowFileDrop event( std::move( dropped ) );
                                 data.EventCallback( event );
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
                                        }
                                    } );

        m_SwapChain = Graphic::SwapChain::Create( m_GLFWWindow );
        // Hand the requested pacing over BEFORE CreateSwapChain picks a present mode (same as Windows).
        m_SwapChain->SetVSync( m_Data.Specification.VSync );

        return Common::MakeSuccess( true );
    }

    MacOSWindow::MacOSWindow( const WindowSpecification& specification )
    {
        m_Data.Specification = specification;
    }

    void MacOSWindow::SetWindowSize( uint32_t width, uint32_t height )
    {
        m_Data.Specification.Width  = width;
        m_Data.Specification.Height = height;
    }

    uint32_t MacOSWindow::GetWidth() const
    {
        return m_Data.Specification.Width;
    }

    uint32_t MacOSWindow::GetHeight() const
    {
        return m_Data.Specification.Height;
    }

    const void* MacOSWindow::GetNativeWindow() const
    {
        return m_GLFWWindow;
    }

    bool MacOSWindow::IsWindowMaximized() const
    {
        return glfwGetWindowMonitor( m_GLFWWindow ) != nullptr;
    }

    bool MacOSWindow::IsWindowMinimized() const
    {
        return false;
    }

    void MacOSWindow::Maximize()
    {
        GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode    = glfwGetVideoMode( monitor );

        m_Data.Specification.Width  = mode->width;
        m_Data.Specification.Height = mode->height;

        glfwSetWindowMonitor( m_GLFWWindow, monitor, 0, 0, mode->width, mode->height, mode->refreshRate );
    }

    void MacOSWindow::ProcessEvents()
    {
        glfwPollEvents();
    }

    void MacOSWindow::PresentFinalImage() const
    {
        Graphic::Renderer::GetInstance().PresentFinalImage();
    }

    void MacOSWindow::PrepareNextFrame() const
    {
        EngineContext::GetInstance().GetRendererContext()->BeginFrame();
    }

    void MacOSWindow::SetTitle( const std::string& title )
    {
        m_Data.Specification.Title = title;
        glfwSetWindowTitle( m_GLFWWindow, title.c_str() );
    }

    void MacOSWindow::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowResize>( [this]( Common::EventWindowResize& e )
                                                        { return this->OnEventWindowResize( e ); } );
    }

    bool MacOSWindow::OnEventWindowResize( Common::EventWindowResize& e )
    {
        m_SwapChain->OnResize( e.width, e.height );

        return false;
    }

    Common::ResultStr<bool> MacOSWindow::SetupSwapChain()
    {
        const auto device = EngineContext::GetInstance().GetDevice();
        return m_SwapChain->CreateSwapChain( device, &m_Data.Specification.Width, &m_Data.Specification.Height );
    }

    MacOSWindow::~MacOSWindow()
    {
    }

} // namespace Desert::Platform::MacOS
