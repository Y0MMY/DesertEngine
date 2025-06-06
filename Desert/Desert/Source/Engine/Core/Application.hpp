#pragma once

#include <string>

#include <Common/Core/Singleton.hpp>
#include <Common/Core/LayerStack.hpp>
#include <Common/Core/Events/WindowEvents.hpp>
#include <Common/Core/Window.hpp>

#include <Common/Core/Core.hpp>

#ifdef EBABLE_IMGUI
#include <Engine/imgui/ImGuiLayer.hpp>
#endif // EBABLE_IMGUI

namespace Desert::Engine
{
    struct ApplicationInfo
    {
        std::string Title;
        uint32_t    Width;
        uint32_t    Height;
    };

    class Application
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
        NO_DISCARD bool OnClose( Common::EventWindowClose& e )
        {
            return true;
        }
        void ProcessEvents( Common::Event& e );

        void ProcessImGui();

    private:
        ApplicationInfo m_ApplicationInfo;

    protected:
        std::shared_ptr<Common::Window> m_Window; // TODO: unique ptr
    private:
        Common::LayerStack m_LayerStack;

        Common::Timestep m_TimeStep;
        Common::Timestep m_Frametime;
        float m_LastFrameTime = 0.0f;
        bool             m_IsRunningApplication = true;

        float    m_FPS        = 0.0f;
        float    m_FPSTimer   = 0.0f;
        uint32_t m_FPSCounter = 0;

        bool m_Minimized = false;

    public:
    };

    Application* CreateApplicaton( int argc, char** argv );
} // namespace Desert::Engine
