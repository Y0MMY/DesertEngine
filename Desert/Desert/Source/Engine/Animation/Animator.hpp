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

        // The model-space (mesh-local) transform of a bone in the CURRENT pose. The pose stores the skinning
        // matrix (global * offset), so the bone's actual placement is recovered by undoing the inverse-bind:
        // global = skinMatrix * inverse(offset). Multiply by the entity's world matrix for a world socket.
        // Returns identity if the index is out of range. (Bone index from Skeleton::FindBoneIndex.)
        [[nodiscard]] glm::mat4 GetBoneModelMatrix( uint32_t boneIndex ) const
        {
            const auto& bones = m_Skeleton.GetBones();
            if ( boneIndex >= bones.size() || boneIndex >= m_CurrentPose.BoneMatrices.size() )
                return glm::mat4( 1.0f );
            return m_CurrentPose.BoneMatrices[boneIndex] * glm::inverse( bones[boneIndex].OffsetMatrix );
        }

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

        // Fills m_CurrentPose with the skeleton's REST/BIND pose (chainGlobal * OffsetMatrix per bone). This is
        // the correct idle pose — an all-identity pose would skin the RAW authored vertices and collapse the
        // mesh. Computed at construction so GetPose() is valid before any clip plays.
        void ComputeBindPose();

        // Returns the clip track that drives skeleton bone `boneIndex`, matched by bone NAME (not by the clip's
        // own bone index). This lets a clip authored against a differently-ordered or skinless export of the
        // same rig still drive the correct bones. Built lazily per clip, cached for the animator's lifetime.
        const BoneTrack* ResolveTrack( const AnimationClip* clip, uint32_t boneIndex ) const;

    private:
        const Skeleton& m_Skeleton;

        // clip -> (skeleton bone index -> its track in that clip, or null). See ResolveTrack.
        mutable std::unordered_map<const AnimationClip*, std::vector<const BoneTrack*>> m_TrackBinding;

        ClipPlayback m_Current;
        ClipPlayback m_Next;

        bool  m_IsBlending    = false;
        float m_BlendTime     = 0.0f;
        float m_BlendDuration = 0.0f;

        float m_PlaybackSpeed = 1.0f;

        Pose m_CurrentPose;
    };
} // namespace Desert::Animation
