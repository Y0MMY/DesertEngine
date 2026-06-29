#include "Animator.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Desert::Animation
{
    struct TRS
    {
        glm::vec3 Translation;
        glm::quat Rotation;
        glm::vec3 Scale;
    };

    TRS Decompose( const glm::mat4& m )
    {
        TRS result;

        result.Translation = glm::vec3( m[3] );

        result.Scale.x = glm::length( glm::vec3( m[0] ) );
        result.Scale.y = glm::length( glm::vec3( m[1] ) );
        result.Scale.z = glm::length( glm::vec3( m[2] ) );

        glm::mat3 rotationMatrix;
        rotationMatrix[0] = glm::vec3( m[0] ) / result.Scale.x;
        rotationMatrix[1] = glm::vec3( m[1] ) / result.Scale.y;
        rotationMatrix[2] = glm::vec3( m[2] ) / result.Scale.z;

        result.Rotation = glm::quat_cast( rotationMatrix );

        return result;
    }

    glm::mat4 Compose( const TRS& trs )
    {
        glm::mat4 T = glm::translate( glm::mat4( 1.0f ), trs.Translation );
        glm::mat4 R = glm::toMat4( trs.Rotation );
        glm::mat4 S = glm::scale( glm::mat4( 1.0f ), trs.Scale );

        return T * R * S;
    }

    Animator::Animator( const Skeleton& skeleton ) : m_Skeleton( skeleton )
    {
        const auto& bones = skeleton.GetBones();
        m_CurrentPose.BoneMatrices.resize( bones.size(), glm::mat4( 1.0f ) );
    }

    // ============================================================
    // Play / CrossFade
    // ============================================================

    void Animator::Play( const AnimationClip& clip, bool loop )
    {
        m_Current    = { &clip, 0.0f, loop };
        m_Next       = {};
        m_IsBlending = false;
    }

    void Animator::CrossFade( const AnimationClip& clip, float duration, bool loop )
    {
        if ( m_Current.Clip == &clip )
            return;

        m_Next = { &clip, 0.0f, loop };

        m_IsBlending    = true;
        m_BlendTime     = 0.0f;
        m_BlendDuration = glm::max( duration, 0.0001f );
    }

    void Animator::Stop()
    {
        m_Current    = {};
        m_Next       = {};
        m_IsBlending = false;
    }

    // ============================================================
    // Update
    // ============================================================

    void Animator::Update( const Common::Timestep& ts )
    {
        float deltaTime = ts.GetSeconds() * m_PlaybackSpeed;

        if ( !m_Current.IsValid() )
            return;

        UpdatePlayback( m_Current, deltaTime );

        if ( m_IsBlending && m_Next.IsValid() )
        {
            UpdatePlayback( m_Next, deltaTime );

            m_BlendTime += deltaTime;
            float alpha = glm::clamp( m_BlendTime / m_BlendDuration, 0.0f, 1.0f );

            CalculateBlendedPose( alpha );

            if ( alpha >= 1.0f )
            {
                m_Current    = m_Next;
                m_Next       = {};
                m_IsBlending = false;
            }
        }
        else
        {
            CalculatePose( m_Current );
        }
    }

    // ============================================================
    // Playback Update
    // ============================================================

    void Animator::UpdatePlayback( ClipPlayback& playback, float dt )
    {
        if ( !playback.IsValid() )
            return;

        float tps = playback.Clip->TicksPerSecond > 0.0f ? playback.Clip->TicksPerSecond : 25.0f;

        playback.Time += dt * tps;

        if ( playback.Loop )
            playback.Time = fmod( playback.Time, playback.Clip->Duration );
        else
            playback.Time = glm::min( playback.Time, playback.Clip->Duration );
    }

    // ============================================================
    // Pose Calculation
    // ============================================================

    void Animator::CalculatePose( const ClipPlayback& playback )
    {
        const auto& bones = m_Skeleton.GetBones();

        for ( uint32_t i = 0; i < bones.size(); ++i )
        {
            if ( bones[i].IsRoot() )
            {
                CalculateBoneTransform( playback, i, glm::mat4( 1.0f ) );
            }
        }
    }

    void Animator::CalculateBlendedPose( float alpha )
    {
        const auto& bones = m_Skeleton.GetBones();

        for ( uint32_t i = 0; i < bones.size(); ++i )
        {
            if ( !bones[i].IsRoot() )
                continue;

            ClipPlayback a = m_Current;
            ClipPlayback b = m_Next;

            CalculateBoneTransform( a, i, glm::mat4( 1.0f ) );

            std::vector<glm::mat4> poseA = m_CurrentPose.BoneMatrices;

            CalculateBoneTransform( b, i, glm::mat4( 1.0f ) );

            std::vector<glm::mat4> poseB = m_CurrentPose.BoneMatrices;

            for ( size_t j = 0; j < poseA.size(); ++j )
            {
                TRS a = Decompose( poseA[j] );
                TRS b = Decompose( poseB[j] );

                TRS blended;
                blended.Translation = glm::mix( a.Translation, b.Translation, alpha );
                blended.Rotation    = glm::slerp( a.Rotation, b.Rotation, alpha );
                blended.Scale       = glm::mix( a.Scale, b.Scale, alpha );

                m_CurrentPose.BoneMatrices[j] = Compose( blended );
            }
        }
    }

    void Animator::CalculateBoneTransform( const ClipPlayback& playback, uint32_t boneIndex,
                                           const glm::mat4& parentTransform )
    {
        const auto& bones = m_Skeleton.GetBones();
        const auto& bone  = bones[boneIndex];

        glm::mat4 localTransform = bone.LocalBindTransform;

        if ( playback.Clip && boneIndex < playback.Clip->Tracks.size() )
        {
            const auto& track = playback.Clip->Tracks[boneIndex];

            if ( !track.PositionKeys.empty() || !track.RotationKeys.empty() || !track.ScaleKeys.empty() )
            {
                localTransform = track.GetTransform( playback.Time );
            }
        }

        glm::mat4 globalTransform = parentTransform * localTransform;

        m_CurrentPose.BoneMatrices[boneIndex] = globalTransform * bone.OffsetMatrix;

        for ( uint32_t i = 0; i < bones.size(); ++i )
        {
            if ( bones[i].ParentBoneID == boneIndex )
            {
                CalculateBoneTransform( playback, i, globalTransform );
            }
        }
    }

    // ============================================================
    // Utilities
    // ============================================================

    const Pose& Animator::GetPose() const
    {
        return m_CurrentPose;
    }

    bool Animator::IsFinished() const
    {
        if ( !m_Current.IsValid() )
            return true;

        if ( m_Current.Loop )
            return false;

        return m_Current.Time >= m_Current.Clip->Duration;
    }

    void Animator::SetTime( float time )
    {
        if ( !m_Current.IsValid() )
            return;

        m_Current.Time = glm::clamp( time, 0.0f, m_Current.Clip->Duration );
        CalculatePose( m_Current );
    }

    void Animator::SetLoop( bool loop )
    {
        if ( m_Current.IsValid() )
            m_Current.Loop = loop;

        if ( m_IsBlending && m_Next.IsValid() )
            m_Next.Loop = loop;
    }

    const AnimationClip* Animator::GetCurrentClip() const
    {
        // While cross-fading, report the TARGET clip: callers (e.g. AnimationECSSystem's name re-sync) treat
        // this as "the clip that should be playing", and seeing the old clip would make them Play() it and
        // snap-cancel the blend.
        return ( m_IsBlending && m_Next.IsValid() ) ? m_Next.Clip : m_Current.Clip;
    }

} // namespace Desert::Animation
