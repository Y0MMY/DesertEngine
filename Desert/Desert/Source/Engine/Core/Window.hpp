#pragma once

#include <string>

#include <Common/Core/Events/Event.hpp>
#include <Common/Core/ResultStr.hpp>
#include <Common/Core/EventRegistry.hpp>

#include <Engine/Graphic/SwapChain.hpp>

namespace Desert::Graphic
{
    class SwapChain;
}

namespace Desert
{
    struct WindowSpecification
    {
        std::string Title  = "Sandbox";
        uint32_t    Width  = 1600;
        uint32_t    Height = 900;
        // A `Decorated` FLAG STOOD HERE and Г12 removed it, together with six Window methods that made
        // up the other half of the same unbuilt thing — see the note on the interface below. It had
        // ZERO readers: the only `glfwWindowHint( GLFW_DECORATED, ... )` calls in the tree live inside
        // each platform's FULLSCREEN branch and decide borderless-over-the-monitor, which is a different
        // question and does not consult this field.
        bool        Fullscreen = false;
        // When Fullscreen (borderless): cover the whole monitor (over the taskbar) if true, else fit the
        // monitor work area (taskbar stays visible).
        bool        FullscreenCoverTaskbar = false;
        bool        VSync                  = true;
    };

    class Window : public Common::EventHandler
    {
    public:
        virtual ~Window()                   = default;
        virtual Common::ResultStr<bool> Init() = 0;

        virtual void ProcessEvents() = 0;

        using EventCallbackFn = std::function<void( Common::Event& )>;

        // SIX METHODS STOOD HERE — GetTitle, SetTitle, SetWindowSize, Maximize, IsWindowMaximized,
        // IsWindowMinimized — implemented on BOTH platforms and called from nowhere. Г12 removed them,
        // and the reason they are worth a paragraph is that they were not six loose ends: together with
        // the `Decorated` flag above (also unread) they are the platform half of ONE feature nobody
        // finished — a custom title bar, the kind UE draws instead of the system frame. Somebody laid
        // the whole platform side, twice, and no caller was ever written.
        //
        // Deleting it is contract §0: unfinished code does not exist in a branch. The intent is not
        // lost — it is filed as У9, and restoring this side is `git revert` of the commit that removed
        // it, which is cheaper than the standing cost of keeping it. A pure virtual with no caller is
        // an instruction to every future implementer to write a body that nothing runs; this one issued
        // that instruction twelve times, six methods across two platforms.
        //
        // What У9 needs beyond a revert: a reader for `Decorated`, the bar itself, and answers for the
        // two things a borderless window loses — the system window gestures on Windows, and the
        // traffic-light buttons plus the fullscreen gesture on macOS.

        virtual void                      SetVSync( bool enabled ) = 0;
        [[nodiscard]] virtual uint32_t    GetWidth() const         = 0;
        [[nodiscard]] virtual uint32_t    GetHeight() const        = 0;
        [[nodiscard]] virtual const void* GetNativeWindow() const  = 0;

        // The frame's own result, passed through rather than swallowed. See Renderer.cpp: this link
        // declared them void, which is one of the three places a failed present used to vanish.
        [[nodiscard]] virtual Common::BoolResultStr PrepareNextFrame() const  = 0;
        [[nodiscard]] virtual Common::BoolResultStr PresentFinalImage() const = 0;

        virtual std::shared_ptr<Graphic::SwapChain> GetWindowSwapChain() = 0;

        virtual void SetEventCallback( const EventCallbackFn& e ) = 0;

        virtual Common::ResultStr<bool>
        SetupSwapChain( ) = 0;

        static std::shared_ptr<Window> Create( const WindowSpecification& specification );
    };
} // namespace Desert