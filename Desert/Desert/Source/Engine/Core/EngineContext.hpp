#pragma once

#include <Engine/Core/Application.hpp>
#include <Engine/Core/Window.hpp>
#include <Engine/Core/Device.hpp>

#include <Engine/Graphic/RendererContext.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanQueue;
    class VulkanSwapChain;
} // namespace Desert::Graphic::API::Vulkan

namespace Desert
{
    class EngineContext final : public Common::Singleton<EngineContext>
    {
    public:
        void Initialize( const std::shared_ptr<Window>& window, const std::shared_ptr<Engine::Device>& device,
                         const std::shared_ptr<Graphic::RendererContext>& rendererContext )
        {
            m_CurrentWindow = window;
            m_Device        = device;
            m_RendererContext = rendererContext;
        }

        uint32_t GetCurrentFrameIndex() const
        {
            return m_CurrentFrameIndex;
        }

        uint32_t GetFramesInFlight() const
        {
            return m_FramesInFlight;
        }

        GLFWwindow* GetCurrentPointerToGLFWwinodw()
        {
            if ( auto window = m_CurrentWindow.lock() ) [[likely]]
            {
                return (GLFWwindow*)window->GetNativeWindow();
            }
            else
            {
                return nullptr;
            }
        }

        std::shared_ptr<Window> GetCurrentWindow()
        {
            if ( auto window = m_CurrentWindow.lock() ) [[likely]]
            {
                return window;
            }
            else
            {
                return nullptr;
            }
        }

        std::shared_ptr<Engine::Device> GetMainDevice()
        {
            if ( auto device = m_Device.lock() ) [[likely]]
            {
                return device;
            }
            else
            {
                return nullptr;
            }
        }

        std::shared_ptr<Graphic::RendererContext> GetRendererContext()
        {
            if ( auto rendererContext = m_RendererContext.lock() ) [[likely]]
            {
                return rendererContext;
            }
            else
            {
                return nullptr;
            }
        }

    private:
        uint32_t                      m_CurrentFrameIndex = 0;
        uint32_t                      m_FramesInFlight    = 2; // TODO: remove hardcode
        std::weak_ptr<Window>         m_CurrentWindow;
        std::weak_ptr<Engine::Device> m_Device;
        std::weak_ptr<Graphic::RendererContext> m_RendererContext;

    private:
        friend class Desert::Engine::Application;
        friend class Desert::Graphic::API::Vulkan::VulkanQueue;
        friend class Desert::Graphic::API::Vulkan::VulkanSwapChain;
    };
} // namespace Desert