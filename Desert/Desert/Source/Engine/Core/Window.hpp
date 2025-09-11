#pragma once

#include <string>

#include <Common/Core/Events/Event.hpp>
#include <Common/Core/Result.hpp>
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
        std::string Title      = "Sandbox";
        uint32_t    Width      = 1600;
        uint32_t    Height     = 900;
        bool        Decorated  = true;
        bool        Fullscreen = false;
        bool        VSync      = true;
    };

    class Window : public Common::EventHandler
    {
    public:
        virtual ~Window()                   = default;
        virtual Common::Result<bool> Init() = 0;

        virtual void ProcessEvents() = 0;

        using EventCallbackFn = std::function<void( Common::Event& )>;

        [[nodiscard]] virtual const std::string& GetTitle() const                     = 0;
        virtual void                             SetTitle( const std::string& title ) = 0;

        virtual void                      SetWindowSize( uint32_t width, uint32_t height ) = 0;
        [[nodiscard]] virtual uint32_t    GetWidth() const                                 = 0;
        [[nodiscard]] virtual uint32_t    GetHeight() const                                = 0;
        virtual void                      SetVSync( bool enabled )                         = 0;
        [[nodiscard]] virtual const void* GetNativeWindow() const                          = 0;

        virtual bool IsWindowMaximized() const = 0;
        virtual bool IsWindowMinimized() const = 0;
        virtual void Maximize()                = 0;

        virtual void PrepareNextFrame() const  = 0;
        virtual void PresentFinalImage() const = 0;

        virtual std::shared_ptr<Graphic::SwapChain> GetWindowSwapChain() = 0;

        virtual void SetEventCallback( const EventCallbackFn& e ) = 0;

        virtual Common::Result<bool>
        SetupSwapChain( ) = 0;

        static std::shared_ptr<Window> Create( const WindowSpecification& specification );
    };
} // namespace Desert