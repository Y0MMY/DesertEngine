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

    void Application::ReportLayerFailure( const char* stage, Common::Layer* layer, const std::string& error )
    {
        // ONCE PER DISTINCT MESSAGE, and the deduplication is the point rather than tidiness. `OnUpdate`
        // and `OnImGuiRender` run every frame, so a failure that persists — a scene that will not begin,
        // a pipeline that will not build — is not one event but sixty a second. Logging each one makes
        // the log unreadable, which is the same outcome as not logging at all; these four results were
        // dropped on the floor before this commit and the cure must not be a flood.
        const std::string key = std::string( stage ) + '|' + layer->GetName() + '|' + error;
        if ( !m_ReportedLayerFailures.insert( key ).second )
            return;

        LOG_ERROR( "[Application] layer '{}' failed in {}: {}", layer->GetName(), stage, error );
    }

    void Application::PushLayer( Common::Layer* layer )
    {
        m_LayerStack.PushLayer( layer );

        const auto attached = layer->OnAttach();
        if ( !attached.IsSuccess() )
        {
            // A layer that did not attach has no resources, and the loop below is about to call OnUpdate
            // on it sixty times a second. Refusing to start is the only answer that does not turn a
            // startup failure into a stream of consequences with a nonzero exit code nowhere in sight —
            // Close's own comment already says why exit 0 on a failed run is the worse error.
            LOG_ERROR( "[Application] layer '{}' failed to attach: {}", layer->GetName(), attached.GetError() );
            Close( 1 );
        }
    }

    void Application::PopLayer( Common::Layer* layer )
    {
        m_LayerStack.PopLayer( layer );

        const auto detached = layer->OnDetach();
        if ( !detached.IsSuccess() )
            ReportLayerFailure( "OnDetach", layer, detached.GetError() );
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
            const auto frameBegun = Graphic::Renderer::GetInstance().BeginFrame();
            if ( !frameBegun.IsSuccess() )
            {
                // END THE RUN, and neither of the two cheaper answers is available here.
                //
                // Recording anyway is what this line did before: everything below writes into the command
                // buffer BeginFrame was supposed to open, and with no open buffer PresentFinalImage still
                // submits and presents — a stale or torn image with no error anywhere, which is the
                // failure shape this project has paid for more than once.
                //
                // `continue` is worse still, and less obviously so: PrepareNextFrame has ALREADY acquired
                // a swapchain image, and skipping the present never gives it back. A few iterations of
                // that and the next acquire blocks forever, so a reported error becomes a hang.
                //
                // Both of BeginFrame's failures are terminal anyway — the window is gone, or
                // vkBeginCommandBuffer refused, which means the device is lost. Exit code 1 so a script
                // that ran the editor headless is told.
                LOG_ERROR( "[Application] BeginFrame failed, ending the run: {}", frameBegun.GetError() );
                Close( 1 );
                break;
            }

            // 4. Update layers (Scene rendering to offscreen buffers)
            for ( Common::Layer* layer : m_LayerStack )
            {
                const auto updated = layer->OnUpdate( Common::Timestep( timestep ) );
                if ( !updated.IsSuccess() )
                    ReportLayerFailure( "OnUpdate", layer, updated.GetError() );
            }

            // 5. UI Rendering
            {
                DESERT_PROFILE_SCOPE( "ImGui Render" );
                for ( Common::Layer* layer : m_LayerStack )
                {
                    const auto rendered = layer->OnImGuiRender();
                    if ( !rendered.IsSuccess() )
                        ReportLayerFailure( "OnImGuiRender", layer, rendered.GetError() );
                }
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
        {
            Common::Layer* layer    = *--it;
            const auto     detached = layer->OnDetach();
            if ( !detached.IsSuccess() )
                ReportLayerFailure( "OnDetach", layer, detached.GetError() );
        }
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
