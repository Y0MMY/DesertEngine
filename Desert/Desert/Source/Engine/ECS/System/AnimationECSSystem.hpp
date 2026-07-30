#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Animation/Graph/AnimGraph.hpp>

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

                // Editor-built runtime rig (Convert to Skinned) has no MeshHandle — prefer it (mirrors the
                // render/pick paths) so a converted mesh can still animate.
                Desert::Mesh* meshBase =
                     skinnedMesh.RuntimeMesh
                          ? static_cast<Desert::Mesh*>( skinnedMesh.RuntimeMesh.get() )
                          : Runtime::ResourceRegistry::GetMeshService()->Get( skinnedMesh.MeshHandle );

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

                const uint64_t sig = skinnedMeshPtr->GetSkeleton().GetSignature();

                // AnimGraph path: the state machine PICKS the clip; the Animator just plays it. Falls back to
                // the CurrentClip path below when no graph is attached.
                if ( anim.Graph && !anim.Graph->States.empty() )
                {
                    if ( !anim.GraphEvaluator )
                    {
                        anim.GraphEvaluator     = std::make_shared<Animation::Graph::Evaluator>( *anim.Graph );
                        anim.BuiltGraphRevision = anim.GraphRevision;
                    }
                    else if ( anim.BuiltGraphRevision != anim.GraphRevision )
                    {
                        // Re-sync after an editor edit WITHOUT resetting the active state / live parameters.
                        anim.GraphEvaluator->SyncGraph( *anim.Graph );
                        anim.BuiltGraphRevision = anim.GraphRevision;
                    }

                    if ( anim.Playing )
                    {
                        // Clip fraction [0,1] drives exit-time transitions.
                        float       norm = 0.0f;
                        const float dur  = anim.Animator->GetDuration();
                        if ( dur > 1e-4f )
                            norm = anim.Animator->GetCurrentTime() / dur;

                        const auto res = anim.GraphEvaluator->Update( norm );
                        if ( res.Current )
                        {
                            if ( const auto* clip = FindClip( sig, res.Current->Clip ) )
                            {
                                const auto* cur = anim.Animator->GetCurrentClip();
                                if ( !cur || cur->AnimationName != clip->AnimationName )
                                {
                                    if ( res.Changed && res.Blend > 0.0f )
                                        anim.Animator->CrossFade( *clip, res.Blend, res.Current->Loop );
                                    else
                                        anim.Animator->Play( *clip, res.Current->Loop );
                                }
                            }
                            anim.Animator->SetPlaybackSpeed( anim.PlaybackSpeed * res.Current->Speed );
                        }

                        anim.Animator->Update( animTs );
                        anim.PendingNotifies = anim.Animator->ConsumeNotifies();
                    }
                    continue;
                }

                if ( !anim.CurrentClip.empty() )
                {
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
        // Resolves a clip by name for the given skeleton signature (AnimGraph state -> clip). Returns a stable
        // pointer into the owning AnimationAsset, or null if the library has no such clip.
        const Animation::AnimationClip* FindClip( uint64_t skeletonSignature, const std::string& name ) const
        {
            if ( name.empty() )
                return nullptr;
            for ( const auto& animAsset : m_AnimationLibrary->GetBySkeleton( skeletonSignature ) )
                if ( animAsset->GetClip().AnimationName == name )
                    return &animAsset->GetClip();
            return nullptr;
        }

    private:
        Animation::AnimationLibrary*          m_AnimationLibrary;
        std::chrono::steady_clock::time_point m_LastTime;
        bool                                  m_HasLast = false;
    };
} // namespace Desert::ECS