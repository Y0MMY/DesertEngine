#pragma once

#include <string>
#include <optional>
#include <cstdint>

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
        // Window size. std::nullopt (the default) = start fullscreen at the monitor's native resolution;
        // set a concrete value for a windowed size.
        std::optional<uint32_t> Width;
        std::optional<uint32_t> Height;
        // Only when starting fullscreen (Width/Height = nullopt): true = cover the taskbar, false = leave
        // the taskbar visible (fit the monitor work area).
        bool FullscreenCoverTaskbar = false;
        bool VSync                  = true;
    };

    class Application
    {
    public:
        Application( const ApplicationInfo& appInfo );
        // Virtual because main owns the concrete app through a std::unique_ptr<Application> (see
        // EntryPoint.hpp): a non-virtual destructor there would destroy the base and leak the derived part.
        virtual ~Application();

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

    public:
        // Ends the run loop after the current frame. Used by the editor's screenshot mode, which renders a
        // fixed number of frames and leaves.
        //
        // @p exitCode becomes the process exit status (see EntryPoint.hpp). It exists because a capture
        // that could not write its PNG used to log the error and then exit 0: a caller reading the exit
        // code was told the shot succeeded. A false success is worse than a false failure — it is the one
        // a script cannot notice.
        void Close( int exitCode = 0 )
        {
            m_IsRunningApplication = false;
            m_ExitCode             = exitCode;
        }

        NO_DISCARD int ExitCode() const
        {
            return m_ExitCode;
        }

    private:
        void Init();
        void Destroy();

    private:
        NO_DISCARD bool OnClose( Common::EventWindowClose& e )
        {
            m_IsRunningApplication = false;
            return true;
        }
        void ProcessEvents( Common::Event& e );

        void ProcessImGui();

        // MEMBER ORDER IS LOAD-BEARING. Members die in REVERSE declaration order, and the window owns the
        // swapchain, its framebuffers and their images — device-owned objects that must be released while
        // the device and the context's VMA allocator are still alive. Declared in the order below they are
        // destroyed window -> device -> context, which is the only order that holds.
        //
        // It used to be the exact opposite -- window declared first, context last -- and that is where the
        // exit-time SEGFAULT came from: VulkanImage2D::Release() dereferenced an already-expired renderer
        // context to reach the allocator. VulkanFramebuffer::Release() carries an `if (allocator)` guard
        // written to survive the same window; that guard is still load-bearing for the editor's
        // process-lifetime thumbnail caches, which are not released deterministically yet.
    private:
        ApplicationInfo m_ApplicationInfo;

        bool m_IsRunningApplication = true;
        bool m_Minimized            = false;
        int  m_ExitCode             = 0;

        std::shared_ptr<Graphic::RendererContext> m_RendererContext;
        std::shared_ptr<Device>                   m_Device;

        Common::LayerStack m_LayerStack;
        EngineStats        m_EngineStats;

    protected:
        std::shared_ptr<Window> m_Window;
    };

    Application* CreateApplicaton( int argc, char** argv );
} // namespace Desert::Engine
