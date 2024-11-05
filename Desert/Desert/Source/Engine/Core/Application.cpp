#include <Engine/Core/Application.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Common/Core/Memory/CommandBuffer.hpp>

#include <GLFW/glfw3.h>

namespace Desert::Engine
{

    Application::Application( const ApplicationInfo& appInfo ) : m_ApplicationInfo( appInfo )
    {
        Common::WindowSpecification specWindow;
        specWindow.Title  = appInfo.Title;
        specWindow.Width  = appInfo.Width;
        specWindow.Height = appInfo.Height;

        m_Window = Common::Window::Create( specWindow );
        m_Window->Init();

        m_Window->SetEventCallback( [this]( Common::Event& e ) { this->ProcessEvents( e ); } );

        /************ initializing data *************/

        const auto& context = Graphic::RendererContext::Create( (GLFWwindow*)m_Window->GetNativeWindow() );
        std::shared_ptr<Graphic::API::Vulkan::VulkanContext> vkContext =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>( context );
        vkContext->CreateVKInstance();
    }

    void Application::Run()
    {
        Init();
        while ( m_IsRunningApplication )
        {
            Common::Timestep timestep;
            for ( const auto& layer : m_LayerStack )
            {
                layer->OnUpdate( m_PrevTimestep );
            }
            m_PrevTimestep = Common::Timestep( timestep - Common::Timestep() );
            m_Window->ProcessEvents();
        }
        Destroy();
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
    }

} // namespace Desert::Engine