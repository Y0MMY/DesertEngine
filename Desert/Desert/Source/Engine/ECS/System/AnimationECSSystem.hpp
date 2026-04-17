#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>

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

                    anim.Animator->Update( ts );

                    if ( !anim.Loop && anim.Animator->IsFinished() )
                    {
                        anim.Playing = false;
                    }
                }
            }
        }

    private:
        Animation::AnimationLibrary* m_AnimationLibrary;
    };
} // namespace Desert::ECS