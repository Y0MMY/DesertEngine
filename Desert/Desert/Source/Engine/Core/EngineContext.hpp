#pragma once

#include <Common/Core/CommonContext.hpp>
#include <Engine/Core/Application.hpp>

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
        void Initialize( const std::shared_ptr<Common::Window>& window )
        {
            Common::CommonContext::CreateInstance().Initialize( window );
        }

        uint32_t GetCurrentFrameIndex() const
        {
            return m_CurrentFrameIndex;
        }

        uint32_t GetFramesInFlight() const
        {
            return m_FramesInFlight;
        }

    private:
        uint32_t m_CurrentFrameIndex = 0;
        uint32_t m_FramesInFlight    = 2; // TODO: remove hardcode

    private:
        friend class Desert::Engine::Application;
        friend class Desert::Graphic::API::Vulkan::VulkanQueue;
        friend class Desert::Graphic::API::Vulkan::VulkanSwapChain;
    };
} // namespace Desert