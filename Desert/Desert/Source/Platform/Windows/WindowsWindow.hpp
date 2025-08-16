#pragma once

#include <Common/Core/Window.hpp>

#include <GLFW/glfw3.h>

namespace Desert::Platform::Windows
{
    class WindowsWindow : public Common::Window
    {
    public:
        WindowsWindow( const Common::WindowSpecification& specification );
        virtual ~WindowsWindow() = default;

        using EventCallbackFn = std::function<void( Common::Event& )>;

        virtual Common::Result<bool> Init() override;

        virtual void ProcessEvents() override;

        [[nodiscard]] virtual const std::string& GetTitle() const override
        {
            return m_Data.Specification.Title;
        }
        virtual void SetTitle( const std::string& title ) override;

        virtual void                   SetWindowSize( uint32_t width, uint32_t height ) override;
        [[nodiscard]] virtual uint32_t GetWidth() const override;
        [[nodiscard]] virtual uint32_t GetHeight() const override;
        virtual void                   SetVSync( bool enabled ) override
        {
            m_Data.Specification.VSync = enabled;
        }
        [[nodiscard]] virtual const void* GetNativeWindow() const override;

        virtual bool IsWindowMaximized() const override;
        virtual bool IsWindowMinimized() const override;
        virtual void Maximize() override;

        virtual void PrepareNextFrame() const override;
        virtual void PresentFinalImage() const override;

        virtual void SetEventCallback( const EventCallbackFn& e ) override
        {
            m_Data.EventCallback = e;
        }

    private:
        struct WindowData
        {
            Common::WindowSpecification Specification;
            EventCallbackFn             EventCallback;
        } m_Data;

        GLFWwindow* m_GLFWWindow;
    };
} // namespace Desert::Platform::Windows
