#pragma once

#include <Engine/ECS/System/System.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Animation/Animator.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Desert::ECS
{
    // UE-style socket attachment. For every entity with a SocketAttachmentComponent, follow a BONE of the
    // target (skinned) entity: take the bone's model-space transform from the target's animator pose, lift it
    // to world space with the target's world matrix, apply the local offset, and write the result into this
    // entity's TransformComponent. Runs AFTER AnimationECSSystem (the pose must be current this frame) and
    // before rendering, so the weapon-in-hand never lags a frame.
    class AttachmentSystem final : public System
    {
    public:
        explicit AttachmentSystem( Core::Scene* scene ) : m_Scene( scene )
        {
        }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer&,
                     const Common::Timestep& ) override
        {
            auto view = registry.view<SocketAttachmentComponent, TransformComponent>();
            for ( auto entity : view )
            {
                auto& socket = view.get<SocketAttachmentComponent>( entity );
                if ( socket.Target.IsNull() || socket.BoneName.empty() )
                    continue;

                // Resolve the target (skinned) entity + its live animator.
                auto targetRef = m_Scene->FindEntityByID( socket.Target );
                if ( !targetRef )
                    continue;
                ECS::Entity target = targetRef->get();
                if ( !target.HasComponent<AnimationComponent>() )
                    continue;
                const auto& anim = target.GetComponent<AnimationComponent>();
                if ( !anim.Animator )
                    continue; // pose not built yet (e.g. not playing) — leave the weapon where it is

                const auto boneIdx = anim.Animator->GetSkeleton().FindBoneIndex( socket.BoneName );
                if ( !boneIdx )
                    continue;

                // World socket = targetWorld * boneModel * localOffset.
                const glm::mat4 targetWorld = target.GetWorldTransform();
                const glm::mat4 boneModel   = anim.Animator->GetBoneModelMatrix( *boneIdx );
                const glm::mat4 offset =
                     glm::translate( glm::mat4( 1.0f ), socket.OffsetTranslation ) *
                     glm::toMat4( glm::quat( socket.OffsetRotation ) ) *
                     glm::scale( glm::mat4( 1.0f ), socket.OffsetScale );
                glm::mat4 world = targetWorld * boneModel * offset;

                // The TransformComponent stores a LOCAL transform. If this entity is parented, convert the
                // world socket back into the parent's space; otherwise local == world.
                if ( registry.has<RelationshipComponent>( entity ) )
                {
                    const auto parent = registry.get<RelationshipComponent>( entity ).Parent;
                    if ( parent != entt::null )
                    {
                        ECS::Entity parentEnt{ parent, registry };
                        world = glm::inverse( parentEnt.GetWorldTransform() ) * world;
                    }
                }

                glm::vec3 scale, translation, skew;
                glm::quat rotation;
                glm::vec4 perspective;
                glm::decompose( world, scale, rotation, translation, skew, perspective );

                auto& tc       = view.get<TransformComponent>( entity );
                tc.Translation = translation;
                tc.Rotation    = glm::eulerAngles( rotation );
                tc.Scale       = scale;
            }
        }

    private:
        Core::Scene* m_Scene = nullptr;
    };
} // namespace Desert::ECS
