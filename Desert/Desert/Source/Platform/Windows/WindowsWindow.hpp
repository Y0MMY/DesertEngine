#pragma once

#include <GLFW/glfw3.h>

#include <Engine/Core/Window.hpp>

#include <Common/Core/Events/WindowEvents.hpp>

namespace Desert::Platform::Windows
{
    class WindowsWindow : public Desert::Window
    {
    public:
        WindowsWindow( const WindowSpecification& specification );
        virtual ~WindowsWindow();

        using EventCallbackFn = std::function<void( Common::Event& )>;

        virtual Common::ResultStr<bool> Init() override;

        virtual void ProcessEvents() override;

        [[nodiscard]] virtual const std::string& GetTitle() const override
        {
            return m_Data.Specification.Title;
        }
        virtual void SetTitle( const std::string& title ) override;

        virtual void                   SetWindowSize( uint32_t width, uint32_t height ) override;
        [[nodiscard]] virtual uint32_t GetWidth() const override;
        [[nodiscard]] virtual uint32_t GetHeight() const override;
        // Runtime toggle: the swapchain picks its present mode at creation, so the new pacing only takes
        // effect once it is rebuilt (OnResize does that). Storing the flag alone — which is all this used
        // to do — left the setting inert.
        virtual void SetVSync( bool enabled ) override
        {
            m_Data.Specification.VSync = enabled;
            if ( m_SwapChain )
            {
                m_SwapChain->SetVSync( enabled );
                m_SwapChain->OnResize( m_Data.Specification.Width, m_Data.Specification.Height );
            }
        }
        [[nodiscard]] virtual const void* GetNativeWindow() const override;

        virtual bool IsWindowMaximized() const override;
        virtual bool IsWindowMinimized() const override;
        virtual void Maximize() override;

        virtual void PrepareNextFrame() const override;
        virtual void PresentFinalImage() const override;

        virtual void OnEvent( Common::Event& e ) override;

        virtual std::shared_ptr<Graphic::SwapChain> GetWindowSwapChain() override
        {
            return m_SwapChain;
        }

        virtual void SetEventCallback( const EventCallbackFn& e ) override
        {
            m_Data.EventCallback = e;
        }

        virtual Common::ResultStr<bool> SetupSwapChain() override;

    private:
        bool OnEventWindowResize( Common::EventWindowResize& e );

    private:
        struct WindowData
        {
            WindowSpecification Specification;
            EventCallbackFn     EventCallback;
        } m_Data;

        GLFWwindow*                         m_GLFWWindow;
        std::shared_ptr<Graphic::SwapChain> m_SwapChain;
    };
} // namespace Desert::Platform::Windows
