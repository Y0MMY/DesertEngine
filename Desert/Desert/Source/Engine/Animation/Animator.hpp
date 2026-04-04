#pragma once

#include <glm/glm.hpp>

#include "Skeleton.hpp"
#include "AnimationClip.hpp"
#include "Pose.hpp"

#include <Common/Core/Timestep.hpp>

namespace Desert::Animation
{
    class Animator
    {
    public:
        explicit Animator( const Skeleton& skeleton );

        void Play( const AnimationClip& clip, bool loop = true );
        void CrossFade( const AnimationClip& clip, float duration, bool loop = true );
        void Stop();

        void Update( const Common::Timestep& ts );

        [[nodiscard]] const Skeleton& GetSkeleton() const
        {
            return m_Skeleton;
        }

        [[nodiscard]] const Pose& GetPose() const;

        [[nodiscard]] bool IsPlaying() const
        {
            return m_Current.Clip != nullptr;
        }

        [[nodiscard]] float GetCurrentTime() const
        {
            return m_Current.Time;
        }

        [[nodiscard]] float GetDuration() const
        {
            return m_Current.Clip ? m_Current.Clip->Duration : 0.0f;
        }

        [[nodiscard]] bool IsFinished() const;

        void SetTime( float time );
        void SetPlaybackSpeed( float speed )
        {
            m_PlaybackSpeed = speed;
        }

        void SetLoop( bool loop );

        [[nodiscard]] const AnimationClip* GetCurrentClip() const;

    private:
        struct ClipPlayback
        {
            const AnimationClip* Clip = nullptr;
            float                Time = 0.0f;
            bool                 Loop = true;

            bool IsValid() const
            {
                return Clip != nullptr;
            }
        };

    private:
        void UpdatePlayback( ClipPlayback& playback, float deltaTime );
        void CalculatePose( const ClipPlayback& playback );
        void CalculateBlendedPose( float alpha );

        void CalculateBoneTransform( const ClipPlayback& playback, uint32_t boneIndex,
                                     const glm::mat4& parentTransform );

    private:
        const Skeleton& m_Skeleton;

        ClipPlayback m_Current;
        ClipPlayback m_Next;

        bool  m_IsBlending    = false;
        float m_BlendTime     = 0.0f;
        float m_BlendDuration = 0.0f;

        float m_PlaybackSpeed = 1.0f;

        Pose m_CurrentPose;
    };
} // namespace Desert::Animation
