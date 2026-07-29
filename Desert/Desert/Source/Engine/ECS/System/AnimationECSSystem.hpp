#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>

#include <chrono>
#include <algorithm>

namespace Desert::ECS
{
    class AnimationECSSystem : public System
    {
    public:
        explicit AnimationECSSystem( Animation::AnimationLibrary* animationLibrary )
             : m_AnimationLibrary( animationLibrary )
        {
        }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                     const Common::Timestep& ts ) override
        {
            // Editor PREVIEW: the gameplay timestep is 0 in Edit mode (gameplay frozen), but animation should
            // still preview when "Playing" is on. So advance by a real wall-clock delta when the gameplay ts
            // is ~0; use the gameplay ts in Play mode. Clamped to avoid huge jumps after a stall.
            const auto  now    = std::chrono::steady_clock::now();
            float       realDt = m_HasLast ? std::chrono::duration<float>( now - m_LastTime ).count() : 0.0f;
            m_LastTime         = now;
            m_HasLast          = true;
            realDt             = std::min( realDt, 0.1f );
            const float effectiveSeconds = ts.GetSeconds() > 1e-6f ? ts.GetSeconds() : realDt;
            const Common::Timestep animTs( effectiveSeconds );
            auto view = registry.view<ECS::SkinnedMeshComponent, ECS::AnimationComponent>();

            for ( auto entity : view )
            {
                auto& skinnedMesh = view.get<ECS::SkinnedMeshComponent>( entity );
                auto& anim        = view.get<ECS::AnimationComponent>( entity );

                auto meshBase = Runtime::ResourceRegistry::GetMeshService()->Get( skinnedMesh.MeshHandle );

                if ( !meshBase || !meshBase->IsSkinned() )
                {
                    anim.Animator.reset();
                    continue;
                }

                auto skinnedMeshPtr = static_cast<Desert::SkinnedMesh*>( meshBase );

                if ( !anim.Animator )
                {
                    anim.Animator = std::make_unique<Animation::Animator>( skinnedMeshPtr->GetSkeleton() );
                }

                if ( !anim.CurrentClip.empty() )
                {
                    const uint64_t sig = skinnedMeshPtr->GetSkeleton().GetSignature();

                    const auto animations = m_AnimationLibrary->GetBySkeleton( sig );

                    for ( const auto& animAsset : animations )
                    {
                        const auto& clip = animAsset->GetClip();

                        if ( clip.AnimationName == anim.CurrentClip )
                        {
                            const auto* current = anim.Animator->GetCurrentClip();

                            if ( !current || current->AnimationName != clip.AnimationName )
                            {
                                anim.Animator->Play( clip, anim.Loop );
                            }

                            break;
                        }
                    }
                }

                else
                {
                    const uint64_t sig = skinnedMeshPtr->GetSkeleton().GetSignature();

                    const auto animations = m_AnimationLibrary->GetBySkeleton( sig );

                    if ( !animations.empty() )
                    {
                        const auto& clip = animations.front()->GetClip();

                        anim.CurrentClip = clip.AnimationName;
                        anim.Animator->Play( clip, anim.Loop );
                    }
                }

                if ( anim.Playing )
                {
                    anim.Animator->SetLoop( anim.Loop );
                    anim.Animator->SetPlaybackSpeed( anim.PlaybackSpeed );

                    anim.Animator->Update( animTs );

                    // Notify markers crossed this frame -> queued for ScriptSystem to dispatch (assigned, so
                    // a paused/cleared frame leaves it empty and nothing re-fires).
                    anim.PendingNotifies = anim.Animator->ConsumeNotifies();

                    if ( !anim.Loop && anim.Animator->IsFinished() )
                    {
                        anim.Playing = false;
                    }
                }
            }
        }

    private:
        Animation::AnimationLibrary*          m_AnimationLibrary;
        std::chrono::steady_clock::time_point m_LastTime;
        bool                                  m_HasLast = false;
    };
} // namespace Desert::ECS