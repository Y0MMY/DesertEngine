#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Common/Core/EventRegistry.hpp>

#include <GLFW/glfw3.h>

namespace Desert::Engine
{
    Application::Application( const ApplicationInfo& appInfo ) : m_ApplicationInfo( appInfo )
    {
        m_Window = Window::Create( WindowSpecification( appInfo.Title, appInfo.Width, appInfo.Height ) );
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
            float    time     = (float)glfwGetTime();
            float    timestep = time - m_LastFrameTime;
            m_LastFrameTime   = time;

            m_EngineStats.Update();

            // 1. Prepare Frame (Acquire next image)
            // This MUST be first because subsequent rendering might depend on the acquired image index
            m_Window->PrepareNextFrame();

            // 2. Start recording commands for this frame
            Graphic::Renderer::GetInstance().BeginFrame();

            // 3. Update layers (Scene rendering to offscreen buffers)
            for ( Common::Layer* layer : m_LayerStack )
                layer->OnUpdate( Common::Timestep( timestep ) );

            // 4. UI Rendering
            // The ImGui Layer implementation itself should handle Begin() and End() 
            // inside its OnImGuiRender if it's the one managing the ImGui context.
            // Or we call it explicitly here if we have a handle to it.
            for ( Common::Layer* layer : m_LayerStack )
                layer->OnImGuiRender();

            m_Window->ProcessEvents();
            
            // 5. Submit all recorded commands and Present
            m_Window->PresentFinalImage();
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
