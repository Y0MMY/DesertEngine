#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Common/Core/EventRegistry.hpp>
#include <Common/Core/Profiler.hpp>

#include <GLFW/glfw3.h>

namespace Desert::Engine
{
    Application::Application( const ApplicationInfo& appInfo ) : m_ApplicationInfo( appInfo )
    {
        WindowSpecification windowSpec;
        windowSpec.Title = appInfo.Title;
        windowSpec.VSync = appInfo.VSync;
        if ( appInfo.Width.has_value() && appInfo.Height.has_value() )
        {
            windowSpec.Width      = *appInfo.Width;
            windowSpec.Height     = *appInfo.Height;
            windowSpec.Fullscreen = false;
        }
        else
        {
            // No explicit size -> fullscreen at the monitor's native resolution (WindowsWindow::Init fills
            // Width/Height from the primary monitor's video mode).
            windowSpec.Fullscreen             = true;
            windowSpec.FullscreenCoverTaskbar = appInfo.FullscreenCoverTaskbar;
        }

        m_Window = Window::Create( windowSpec );
        m_Window->Init();

        // 1. Create RendererContext (Vulkan Instance)
        m_RendererContext = Graphic::RendererContext::Create( m_Window );
        
        // 2. Partially initialize EngineContext
        EngineContext::CreateInstance().Initialize( m_Window, nullptr, m_RendererContext );

        // 3. Create Device
        m_Device = Device::Create();
        
        // 4. Register Device
        EngineContext::GetInstance().SetDevice( m_Device );

        // 5. Init Context (Allocators)
        m_RendererContext->Init();

        // 6. Setup SwapChain
        auto swapChainResult = m_Window->SetupSwapChain();
        DESERT_VERIFY( swapChainResult.IsSuccess(), "Failed to setup SwapChain" );

        // 7. Initialize Global Renderer
        Graphic::Renderer::CreateInstance().Init();

        m_Window->SetEventCallback( [this]( Common::Event& e ) { ProcessEvents( e ); } );
    }

    Application::~Application()
    {
        // Nothing below may be recorded into, or referenced by, a command buffer the GPU has not finished
        // with. Run() has already presented its last frame, but presentation only queues the work.
        if ( m_Device )
            m_Device->WaitIdle();

        // Everything the renderer generated at Init() (the BRDF LUT, the fallback textures, the API
        // object) and every GPU resource the registries handed out lives in a static that outlives this
        // object. Released here, while the device and the allocator are still alive, because a static
        // destructor cannot be ordered against them.
        Graphic::Renderer::GetInstance().Shutdown();

        // Members then die window -> device -> context; see the note on the declarations.
    }

    void Application::ProcessEvents( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowClose>( [this]( Common::EventWindowClose& e )
                                                       { return this->OnClose( e ); } );

        for ( auto it = m_LayerStack.end(); it != m_LayerStack.begin(); )
        {
            ( *--it )->OnEvent( e );
            if ( e.m_Handled )
                break;
        }
    }

    void Application::PushLayer( Common::Layer* layer )
    {
        m_LayerStack.PushLayer( layer );
        layer->OnAttach();
    }

    void Application::PopLayer( Common::Layer* layer )
    {
        m_LayerStack.PopLayer( layer );
        layer->OnDetach();
    }

    void Application::Run()
    {
        float m_LastFrameTime = 0.0f;
        while ( m_IsRunningApplication )
        {
            // Frame boundary for the profiler (publishes last frame, flips Optick's frame). Placed at the
            // very top so every scope below — acquire, update, UI, present — is attributed to this frame.
            DESERT_PROFILE_FRAME( "Frame" );

            float    time     = (float)glfwGetTime();
            float    timestep = time - m_LastFrameTime;
            m_LastFrameTime   = time;

            m_EngineStats.Update();

            // 1. Pump GLFW events before touching any GPU resources.
            // Resize/close callbacks can destroy descriptor sets and framebuffers; they must
            // fire outside of a recording session, before PrepareNextFrame acquires the image.
            {
                DESERT_PROFILE_SCOPE( "ProcessEvents" );
                m_Window->ProcessEvents();
            }

            // 2. Prepare Frame (Acquire next image) — CPU blocks here if the GPU is behind / vsync-gated.
            {
                DESERT_PROFILE_SCOPE( "PrepareNextFrame (Acquire)" );
                m_Window->PrepareNextFrame();
            }

            // 3. Start recording commands for this frame
            Graphic::Renderer::GetInstance().BeginFrame();

            // 4. Update layers (Scene rendering to offscreen buffers)
            for ( Common::Layer* layer : m_LayerStack )
                layer->OnUpdate( Common::Timestep( timestep ) );

            // 5. UI Rendering
            {
                DESERT_PROFILE_SCOPE( "ImGui Render" );
                for ( Common::Layer* layer : m_LayerStack )
                    layer->OnImGuiRender();
            }

            // 6. Submit all recorded commands and Present — CPU blocks here on submit/present (GPU-bound/vsync).
            {
                DESERT_PROFILE_SCOPE( "PresentFinalImage (Submit)" );
                m_Window->PresentFinalImage();
            }
        }

        // Window closed: detach layers (top-down) so each releases its resources and clears its
        // session state — e.g. EditorLayer::OnDetach removes the crash-recovery lock. Nothing else
        // calls OnDetach (the LayerStack dtor is empty), so without this a normal quit looked like
        // an unclean exit and the recovery prompt reappeared on every launch.
        for ( auto it = m_LayerStack.end(); it != m_LayerStack.begin(); )
            ( *--it )->OnDetach();
    }

    void Application::Init()
    {
    }

    void Application::Destroy()
    {
    }

    void Application::ProcessImGui()
    {
    }

} // namespace Desert::Engine
