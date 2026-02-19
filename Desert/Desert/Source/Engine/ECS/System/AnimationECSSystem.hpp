#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>

namespace Desert::ECS
{
    class AnimationECSSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, const Common::Timestep& ts ) override
        {
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

                auto skinnedMeshPtr = std::static_pointer_cast<Desert::SkinnedMesh>( meshBase );

                if ( !anim.Animator )
                {
                    anim.Animator = std::make_unique<Animation::Animator>( skinnedMeshPtr->GetSkeleton() );
                }

                if ( !anim.CurrentClip.empty() )
                {
                    const auto clip = skinnedMeshPtr->GetSkeleton().GetClip( anim.CurrentClip );

                    if ( clip )
                    {
                        if ( anim.Animator->GetCurrentClip() != clip.get() )
                        {
                            anim.Animator->Play( *clip, anim.Loop );
                        }
                    }
                }

                else
                {
                    const auto& clips = skinnedMeshPtr->GetSkeleton().GetClips();

                    if ( !clips.empty() )
                    {
                        const auto& firstPair    = *clips.begin();
                        const auto& firstClipPtr = firstPair.second;

                        if ( firstClipPtr )
                        {
                            anim.CurrentClip = firstPair.first;
                            anim.Animator->Play( *firstClipPtr, anim.Loop );
                        }
                    }
                }

                if ( anim.Playing && anim.Animator->IsPlaying() )
                {
                    anim.Animator->Update( ts.GetSeconds() * anim.PlaybackSpeed );
                }
            }
        }
    };
} // namespace Desert::ECS