#include <Engine/Core/Application.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Common/Core/Memory/CommandBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Core/EngineContext.h>
#include <Engine/Graphic/Renderer.hpp>

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
        EngineContext::GetInstance().m_CurrentWindow = m_Window;

        m_Window->SetEventCallback( [this]( Common::Event& e ) { this->ProcessEvents( e ); } );

        Graphic::Renderer::CreateInstance().Init();
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

            auto&       con  = Graphic::Renderer::GetInstance().GetRendererContext();
            const auto& vcon = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>( con );
            auto&       q    = vcon->GetVulkanQueue();
            auto&       s    = vcon->GetVulkanSwapChain();

            for ( int i = 0; i < q->GetDrawCommandBuffers().size(); i++ )
            {
                Graphic::API::Vulkan::VulkanRendererAPI::ClearImage_Vulkan_test( q->GetDrawCommandBuffers()[i],
                                                                                 s->GetVKImage()[i] );
            }
            q->Present();

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