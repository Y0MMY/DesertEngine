#include "Animator.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Desert::Animation
{
    Animator::Animator( const Skeleton& skeleton ) : m_Skeleton( skeleton )
    {
        const auto& bones = skeleton.GetBones();
        m_CurrentPose.BoneMatrices.resize( bones.size(), glm::mat4( 1.0f ) );
    }

    void Animator::Play( const AnimationClip& clip, bool loop )
    {
        m_CurrentClip = &clip;
        m_CurrentTime = 0.0f;
        m_Loop        = loop;
    }

    void Animator::Stop()
    {
        m_CurrentClip = nullptr;
        m_CurrentTime = 0.0f;
    }

    void Animator::Update( float deltaTime )
    {
        if ( !m_CurrentClip )
            return;

        const float ticksPerSecond = m_CurrentClip->TicksPerSecond > 0.0f ? m_CurrentClip->TicksPerSecond : 25.0f;

        m_CurrentTime += deltaTime * ticksPerSecond;

        if ( m_Loop )
        {
            m_CurrentTime = fmod( m_CurrentTime, m_CurrentClip->Duration );
        }
        else
        {
            if ( m_CurrentTime > m_CurrentClip->Duration )
                m_CurrentTime = m_CurrentClip->Duration;
        }

        const auto& bones = m_Skeleton.GetBones();
        for ( uint32_t i = 0; i < bones.size(); ++i )
        {
            if ( bones[i].IsRoot() )
            {
                CalculateBoneTransform( i, glm::mat4( 1.0f ) );
            }
        }
    }

    void Animator::CalculateBoneTransform( uint32_t boneIndex, const glm::mat4& parentTransform )
    {
        const auto& bones = m_Skeleton.GetBones();
        const auto& bone  = bones[boneIndex];

        glm::mat4 localTransform = bone.LocalBindTransform;

        if ( m_CurrentClip )
        {
            auto trackIt = m_CurrentClip->Tracks.find( bone.Name );
            if ( trackIt != m_CurrentClip->Tracks.end() )
            {
                localTransform = trackIt->second.GetTransform( m_CurrentTime );
            }
        }

        glm::mat4 globalTransform = parentTransform * localTransform;

        m_CurrentPose.BoneMatrices[boneIndex] = globalTransform * bone.OffsetMatrix;

        for ( uint32_t i = 0; i < bones.size(); ++i )
        {
            if ( bones[i].ParentBoneID == boneIndex )
            {
                CalculateBoneTransform( i, globalTransform );
            }
        }
    }

    const Pose& Animator::GetPose() const
    {
        return m_CurrentPose;
    }
} // namespace Desert::Animation
