#pragma once

#include <glm/glm.hpp>

#include "Skeleton.hpp"
#include "AnimationClip.hpp"
#include "Pose.hpp"

namespace Desert::Animation
{
    class Animator
    {
    public:
        explicit Animator( const Skeleton& skeleton );

        void Play( const AnimationClip& clip, bool loop = true );
        void Stop();

        void Update( float deltaTime );

        [[nodiscard]] const Pose& GetPose() const;

        [[nodiscard]] bool IsPlaying() const
        {
            return m_CurrentClip != nullptr;
        }

        [[nodiscard]] const auto& GetCurrentClip() const
        {
            return m_CurrentClip;
        }

    private:
        void CalculateBoneTransform( uint32_t boneIndex, const glm::mat4& parentTransform );

    private:
        const Skeleton&      m_Skeleton;
        const AnimationClip* m_CurrentClip = nullptr;

        float m_CurrentTime = 0.0f; // in ticks
        bool  m_Loop        = true;

        Pose m_CurrentPose;
    };
} // namespace Desert::Animation
