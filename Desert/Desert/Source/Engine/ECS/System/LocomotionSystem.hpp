#pragma once

#include <Engine/ECS/System/System.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Core/Scene.hpp>

#include <Engine/Geometry/ProceduralCharacterFactory.hpp>
#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Animation/ProceduralCharacterAnimations.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::ECS
{
    // Maps a character's MOVEMENT STATE (planar speed + on-ground, produced by PhysicsECSSystem) to a locomotion
    // CLIP on its skinned child. This is deliberately SEPARATE from the physics system: physics is a mechanism
    // that only outputs state (CharacterControllerComponent.CurrentSpeed / .OnGround); deciding "which animation
    // for this state" is behaviour and does not belong inside the physics step. (A future AnimationStateMachine
    // or a Lua policy could replace this system without touching physics.) Play-only. Runs AFTER PhysicsECSSystem.
    class LocomotionSystem final : public System
    {
    public:
        explicit LocomotionSystem( Core::Scene* scene ) : m_Scene( scene )
        {
        }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer&,
                     const Common::Timestep& ) override
        {
            if ( !m_Scene || m_Scene->GetState() != Core::Scene::SceneState::Play )
                return;

            auto view = registry.view<CharacterControllerComponent>();
            for ( auto entity : view )
            {
                const auto& cc = view.get<CharacterControllerComponent>( entity );
                Drive( registry, entity, cc.CurrentSpeed, cc.OnGround );
            }
        }

    private:
        // Selects + plays the procedural locomotion clip on the character's skinned child (the one carrying the
        // humanoid rig), based on planar speed. Only changes the clip on transition (so it doesn't restart every
        // frame). No-op for a non-humanoid rig (the procedural clips are bone-indexed for that skeleton).
        static void Drive( entt::registry& registry, entt::entity character, float speed, bool onGround )
        {
            if ( !registry.has<RelationshipComponent>( character ) )
                return;

            for ( entt::entity child : registry.get<RelationshipComponent>( character ).Children )
            {
                if ( !registry.has<SkinnedMeshComponent>( child ) || !registry.has<AnimationComponent>( child ) )
                    continue;

                auto& anim = registry.get<AnimationComponent>( child );
                if ( !anim.Animator )
                    return; // created by AnimationECSSystem next frame

                auto* base = Runtime::ResourceRegistry::GetMeshService()->Get(
                     registry.get<SkinnedMeshComponent>( child ).MeshHandle );
                if ( !base || !base->IsSkinned() )
                    return;
                if ( static_cast<SkinnedMesh*>( base )->GetSkeleton().GetSignature() !=
                     Geometry::ProceduralCharacterFactory::GetHumanoidSkeletonSignature() )
                    return; // a different rig — our procedural clips wouldn't map onto it

                const Animation::AnimationClip* clip = nullptr;
                const char*                     name = nullptr;
                if ( !onGround )
                {
                    clip = &Animation::ProceduralCharacterAnimations::Jump();
                    name = "Jump";
                }
                else if ( speed < 0.2f )
                {
                    clip = &Animation::ProceduralCharacterAnimations::Idle();
                    name = "Idle";
                }
                else if ( speed <= 6.5f ) // absolute walk/run split (sprint speed lands above this)
                {
                    clip = &Animation::ProceduralCharacterAnimations::Walk();
                    name = "Walk";
                }
                else
                {
                    clip = &Animation::ProceduralCharacterAnimations::Run();
                    name = "Run";
                }

                if ( anim.CurrentClip != name )
                {
                    // CrossFade (not Play) so idle<->walk<->run transitions blend smoothly instead of popping.
                    anim.Animator->CrossFade( *clip, 0.18f, true );
                    anim.CurrentClip = name;
                    anim.Playing     = true;
                    anim.Loop        = true;
                }
                return;
            }
        }

    private:
        Core::Scene* m_Scene = nullptr;
    };
} // namespace Desert::ECS
