#include <Engine/Core/Application.hpp>

namespace Desert::Engine
{

    Application::Application( const ApplicationInfo& appInfo ) : m_ApplicationInfo( appInfo )
    {
        Common::WindowSpecification specWindow;
        specWindow.Title  = appInfo.Title;
        specWindow.Width  = 200;
        specWindow.Height = 200;

        m_Window = Common::Window::Create( specWindow );
        m_Window->Init();

        m_Window->SetEventCallback( [this]( Common::Event& e ) { this->ProcessEvents( e ); } );
    }

    void Application::Run()
    {
        while ( m_IsRunningApplication )
        {
            m_Window->ProcessEvents();
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

    void Application::ProcessEvents( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowClose>(
             [this]( const Common::EventWindowClose& e ) -> bool
             {
                 m_IsRunningApplication = false;
                 return true;
             } );
    }

} // namespace Desert::Engine