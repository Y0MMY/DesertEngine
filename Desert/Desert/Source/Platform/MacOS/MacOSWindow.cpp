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

        // A closed lid with no external display leaves CGGetOnlineDisplayList empty: glfwGetPrimaryMonitor
        // returns NULL and glfwGetVideoMode asserts on it. The window itself is still creatable (the
        // headless --shot path renders offscreen through it), so fall back to the authored size instead of
        // the video mode — but say so, because a fullscreen request cannot be honoured without a monitor.
        GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode    = monitor ? glfwGetVideoMode( monitor ) : nullptr;
        if ( !monitor )
            LOG_ERROR( "No monitor is online (lid closed?): window falls back to {}x{}, fullscreen ignored", width,
                       height );
        int        posX = 0, posY = 0;
        bool       setPos       = false;
        const bool coverTaskbar = m_Data.Specification.Fullscreen && m_Data.Specification.FullscreenCoverTaskbar;

        // Covering the taskbar means covering the Dock and the menu bar too: that window has no frame
        // whatever the specification says, because there is nowhere on the monitor to put one.
        const bool wantsFrame = m_Data.Specification.Decorated && !coverTaskbar;

        if ( m_Data.Specification.Fullscreen && monitor && mode )
        {
            if ( coverTaskbar )
            {
                width  = mode->width;
                height = mode->height;
                posX   = 0;
                posY   = 0;
                setPos = true;
            }
            else
            {
                // MAXIMIZED to the work area: leaves the menu bar/Dock visible. The OS positions/sizes
                // it; we just give a sane restore size.
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

        // THE FRAME IS TAKEN OFF AFTER CREATION, NOT ASKED FOR THROUGH THE HINT, AND THE DIFFERENCE IS
        // MEASURED. `glfwWindowHint( GLFW_DECORATED, GLFW_FALSE )` makes Cocoa build the NSWindow with
        // NSWindowStyleMaskBorderless and WITHOUT NSWindowStyleMaskResizable (cocoa_window.m only adds
        // the resizable bit on the decorated branch), and AppKit's -zoom: does nothing to a window that
        // is not resizable. Probed on this machine against a freshly created 800x600 borderless window:
        //
        //   hint  GLFW_DECORATED=FALSE : GLFW_MAXIMIZED reads 1 on the untouched 800x600 window,
        //                               glfwMaximizeWindow leaves it 800x600, restore is a no-op.
        //   create decorated, then
        //   glfwSetWindowAttrib(FALSE) : GLFW_MAXIMIZED reads 0 -> 1 -> 0, and Maximize gives the whole
        //                               work area, 2056x1289 at 0,40.
        //
        // _glfwSetWindowDecoratedCocoa only clears Titled/Closable and sets Borderless, so the resizable
        // bit the decorated creation put there survives. IsWindowMaximized asks the OS precisely so that
        // there is one owner of that fact — with the hint, the OS's own answer is the wrong one.
        if ( !wantsFrame && m_GLFWWindow )
        {
            glfwSetWindowAttrib( m_GLFWWindow, GLFW_DECORATED, GLFW_FALSE );
            // Dropping the title bar grows the CONTENT rect (setStyleMask keeps the frame rect), and a
            // window that was created maximized is no longer zoomed afterwards. Re-issue it so the
            // OS's own maximized flag and the window agree from the first frame.
            if ( m_Data.Specification.Fullscreen && !coverTaskbar )
                glfwMaximizeWindow( m_GLFWWindow );
        }

        if ( setPos && m_GLFWWindow )
            glfwSetWindowPos( m_GLFWWindow, posX, posY );

        // The open size differs from the restore size whenever the window was maximized or undecorated
        // above -> sync the spec to the real client size so the swapchain/camera use the correct
        // dimensions.
        if ( m_GLFWWindow )
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

        // WRITTEN BACK, because IsDecorated() answers out of this field and a caller that asks "do I own
        // the frame?" must get what HAPPENED, not what was requested. A fullscreen-over-the-taskbar
        // window is frameless whatever the specification said.
        m_Data.Specification.Decorated = wantsFrame;

        LOG_INFO( "The Window (macOS) was created with: Title = {}, Width = {}, Height = {}, Frame = {}",
                  m_Data.Specification.Title.c_str(), m_Data.Specification.Width, m_Data.Specification.Height,
                  wantsFrame ? "system" : "drawn by the application" );

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
                            []( GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/ )
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
                                    []( GLFWwindow* window, int button, int action, int /*mods*/ )
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

    void MacOSWindow::SetTitle( const std::string& title )
    {
        m_Data.Specification.Title = title;
        glfwSetWindowTitle( m_GLFWWindow, title.c_str() );
    }

    // THE OS IS TOLD, not just the cache. The version Г12 deleted assigned Width/Height and stopped
    // there — a setter that set nothing, and one nobody noticed because nobody called it. The spec fields
    // are a cache of the OS's answer: the resize callback writes them when the move actually happens.
    void MacOSWindow::SetWindowSize( uint32_t width, uint32_t height )
    {
        glfwSetWindowSize( m_GLFWWindow, (int)width, (int)height );
    }

    void MacOSWindow::SetWindowPos( int x, int y )
    {
        glfwSetWindowPos( m_GLFWWindow, x, y );
    }

    void MacOSWindow::GetWindowPos( int& x, int& y ) const
    {
        glfwGetWindowPos( m_GLFWWindow, &x, &y );
    }

    // glfwMaximizeWindow, not glfwSetWindowMonitor. The deleted version made the window FULLSCREEN on the
    // primary monitor and called it "Maximize", which is a different thing with a different exit: it
    // cannot be restored by a restore button, it moves the window to a monitor the user did not pick, and
    // it made IsWindowMaximized (which asked glfwGetWindowMonitor) answer "maximized" for a window that
    // was merely fullscreen. Two wrong answers that agreed with each other.
    void MacOSWindow::Maximize()
    {
        glfwMaximizeWindow( m_GLFWWindow );
    }

    void MacOSWindow::Restore()
    {
        glfwRestoreWindow( m_GLFWWindow );
    }

    void MacOSWindow::Minimize()
    {
        glfwIconifyWindow( m_GLFWWindow );
    }

    bool MacOSWindow::IsWindowMaximized() const
    {
        return glfwGetWindowAttrib( m_GLFWWindow, GLFW_MAXIMIZED ) == GLFW_TRUE;
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

    void MacOSWindow::ProcessEvents()
    {
        glfwPollEvents();
    }

    Common::BoolResultStr MacOSWindow::PresentFinalImage() const
    {
        return Graphic::Renderer::GetInstance().PresentFinalImage();
    }

    Common::BoolResultStr MacOSWindow::PrepareNextFrame() const
    {
        return EngineContext::GetInstance().GetRendererContext()->BeginFrame();
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
