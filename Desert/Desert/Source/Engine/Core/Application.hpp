#pragma once

#include <string>

#include <Common/Core/Singleton.hpp>
#include <Common/Core/LayerStack.hpp>
#include <Common/Core/Events/WindowEvents.hpp>
#include <Common/Core/Window.hpp>

namespace Desert::Engine
{
    struct ApplicationInfo
    {
        std::string Title;
    };

    class Application : public Common::Singleton<Application>
    {
    public:
        Application( const ApplicationInfo& appInfo );
        ~Application() = default;

        virtual void OnCreate()  = 0;
        virtual void OnDestroy() = 0;

        void PushLayer( Common::Layer* layer );
        void PopLayer( Common::Layer* layer );

        void Run();

    private:
        bool OnClose( Common::EventWindowClose& e )
        {
            return true;
        }
        void ProcessEvents( Common::Event& e );

    private:
        ApplicationInfo                 m_ApplicationInfo;
        std::shared_ptr<Common::Window> m_Window;

        Common::LayerStack m_LayerStack;

        bool m_IsRunningApplication = true;
    };

    Application* CreateApplicaton( int argc, char** argv );
} // namespace Desert::Engine
