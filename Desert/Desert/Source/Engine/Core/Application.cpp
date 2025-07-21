#include <Engine/Core/Application.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Common/Core/EventRegistry.hpp>

#include <GLFW/glfw3.h>

namespace Desert::Engine
{
    Application::Application( const ApplicationInfo& appInfo ) : m_ApplicationInfo( appInfo )
    {
        EngineContext::CreateInstance();

        Common::WindowSpecification specWindow;
        specWindow.Title  = appInfo.Title;
        specWindow.Width  = appInfo.Width;
        specWindow.Height = appInfo.Height;

        m_Window = Common::Window::Create( specWindow );
        m_Window->Init();
        EngineContext::GetInstance().Initialize( m_Window );

        m_Window->SetEventCallback( [this]( Common::Event& e ) { this->ProcessEvents( e ); } );

        Graphic::Renderer::CreateInstance().Init();
    }

    void Application::Run()
    {
        OnCreate();
        Init();

        static float FpsUpdateInterval    = 1.0f;
        static float FpsUpdateAccumulator = 0.0f;
        static float FPS                  = 0.0f;

        while ( m_IsRunningApplication )
        {
            m_Window->ProcessEvents();
            if ( !m_Minimized )
            {
                for ( const auto& layer : m_LayerStack )
                {
                    const auto& result = layer->OnUpdate( m_TimeStep );
                    if ( !result )
                    {
                        throw std::logic_error( result.GetError() );
                    }
                }
            }

            float currentTime = glfwGetTime();
            m_Frametime       = Common::Timestep( currentTime - m_LastFrameTime );
            m_TimeStep        = Common::Timestep( glm::min<float>( m_Frametime.GetSeconds(), 0.0333f ) );
            m_LastFrameTime   = currentTime;

            m_FPSCounter++;
            FpsUpdateAccumulator += m_Frametime.GetSeconds();

            if ( FpsUpdateAccumulator >= FpsUpdateInterval )
            {
                FPS                  = static_cast<float>( m_FPSCounter ) / FpsUpdateAccumulator;
                m_FPSCounter         = 0;
                FpsUpdateAccumulator = 0.0f;

                m_Window->SetTitle( "FPS: " + std::to_string( (uint32_t)FPS ) );
            }
        }
        Destroy();
    }

    void Application::PushLayer( Common::Layer* layer )
    {
        m_LayerStack.PushLayer( layer );
    }

    void Application::PopLayer( Common::Layer* layer )
    {
        m_LayerStack.PopLayer( layer );
        layer->OnDetach();
    }

    void Application::ProcessEvents( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowClose>(
             [this]( const Common::EventWindowClose& e ) -> bool
             {
                 m_IsRunningApplication = false;
                 return true;
             } );

        eventManager.Notify<Common::EventWindowResize>(
             [this]( const Common::EventWindowResize& e ) -> bool
             {
                 int width = e.width, height = e.height;
                 if ( width == 0 || height == 0 )
                 {
                     m_Minimized = true;
                     return false;
                 }
                 m_Minimized = false;

                 return true;
             } );

        Common::EventHandler::ForEach( [&]( Common::EventHandler& handler ) { handler.OnEvent( e ); } );
    }

    void Application::Init()
    {
        for ( const auto& layer : m_LayerStack )
        {
            layer->OnAttach();
        }
    }

    void Application::Destroy()
    {
        for ( const auto& layer : m_LayerStack )
        {
            layer->OnDetach();
            delete layer;
        }

        Graphic::Renderer::GetInstance().Shutdown();
    }

    void Application::ProcessImGui()
    {
    }

} // namespace Desert::Engine