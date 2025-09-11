#pragma once

#include <string>

#include <Common/Core/Singleton.hpp>
#include <Common/Core/LayerStack.hpp>
#include <Common/Core/Events/WindowEvents.hpp>
#include <Common/Core/Core.hpp>

#include "EngineStats.hpp"
#include <Engine/Core/Window.hpp>
#include <Engine/Core/Device.hpp>

#include <Engine/Graphic/RendererContext.hpp>

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
        bool        Fullscreen = true;
    };

    class Application
    {
    public:
        Application( const ApplicationInfo& appInfo );
        ~Application() = default;

        const auto& GetEngineStats() const
        {
            return m_EngineStats;
        }

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
        std::shared_ptr<Window> m_Window;

    private:
        Common::LayerStack m_LayerStack;
        EngineStats        m_EngineStats;

        bool m_IsRunningApplication = true;
        bool m_Minimized            = false;

        std::shared_ptr<Device>                   m_Device;
        std::shared_ptr<Graphic::RendererContext> m_RendererContext;

    public:
    };

    Application* CreateApplicaton( int argc, char** argv );
} // namespace Desert::Engine
