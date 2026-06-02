#pragma once

#include <Common/Core/Singleton.hpp>
#include <cstdint>

namespace Desert::Engine
{
    /**
     * @brief Manages frame-level synchronization and indexing.
     */
    class FrameManager final : public Common::Singleton<FrameManager>
    {
    public:
        void Initialize( uint32_t maxFramesInFlight )
        {
            m_MaxFramesInFlight = maxFramesInFlight;
            m_CurrentFrameIndex = 0;
            m_AbsoluteFrameCount = 0;
        }

        /**
         * @brief Advances to the next frame.
         */
        void NextFrame()
        {
            m_CurrentFrameIndex = ( m_CurrentFrameIndex + 1 ) % m_MaxFramesInFlight;
            m_AbsoluteFrameCount++;
        }

        [[nodiscard]] uint32_t GetCurrentFrameIndex() const { return m_CurrentFrameIndex; }
        [[nodiscard]] uint32_t GetMaxFramesInFlight() const { return m_MaxFramesInFlight; }
        [[nodiscard]] uint64_t GetAbsoluteFrameCount() const { return m_AbsoluteFrameCount; }

    private:
        uint32_t m_CurrentFrameIndex = 0;
        uint32_t m_MaxFramesInFlight = 2;
        uint64_t m_AbsoluteFrameCount = 0;
    };

} // namespace Desert::Engine
