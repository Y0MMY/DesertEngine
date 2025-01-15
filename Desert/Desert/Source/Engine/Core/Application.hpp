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
        uint32_t    Width;
        uint32_t    Height;
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

        const auto& GetWindow() const
        {
            return m_Window;
        }

        void Run();

    private:
        void Init();
        void Destroy();

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

        Common::Timestep m_PrevTimestep;
        bool             m_IsRunningApplication = true;

    public:
        inline static std::vector<std::function<void( Common::Event& )>> s_RegisteredEvents;
    };

    Application* CreateApplicaton( int argc, char** argv );
} // namespace Desert::Engine
