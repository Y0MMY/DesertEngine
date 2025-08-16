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
        specWindow.Title      = appInfo.Title;
        specWindow.Width      = appInfo.Width;
        specWindow.Height     = appInfo.Height;
        specWindow.Fullscreen = appInfo.Fullscreen;

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

        while ( m_IsRunningApplication )
        {
            m_Window->ProcessEvents();
            if ( !m_Minimized )
            {
                for ( const auto& layer : m_LayerStack )
                {
                    const auto& result = layer->OnUpdate( m_EngineStats.GetDeltaTime() );
                    if ( !result )
                    {
                        throw std::logic_error( result.GetError() );
                    }
                }
            }

            m_EngineStats.Update();
            m_Window->SetTitle( m_EngineStats.GetFormattedStats() );
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