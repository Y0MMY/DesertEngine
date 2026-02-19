#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Animation/Animator.hpp>

namespace Desert::ECS
{
    class MeshECSSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, const Common::Timestep& ts ) override
        {
            const auto& renderer = m_Renderer.lock();
            if ( !renderer )
                return;

            /* =========================
               STATIC MESHES
               ========================= */
            {
                auto view = registry.view<StaticMeshComponent, TransformComponent>();

                view.each(
                     [&]( entt::entity entity, const StaticMeshComponent& mesh,
                          const TransformComponent& transform )
                     {
                         std::shared_ptr<Desert::Mesh> targetMesh = nullptr;

                         switch ( mesh.GetMeshType() )
                         {
                             case StaticMeshComponent::Type::Primitive:
                             {
                                 if ( !mesh.PrimitiveShape )
                                     return;

                                 targetMesh = PrimitiveMeshFactory::GetPrimitive( *mesh.PrimitiveShape );
                                 break;
                             }

                             case StaticMeshComponent::Type::Asset:
                             {
                                 if ( !mesh.MeshHandle || !mesh.Material )
                                     return;

                                 targetMesh = Runtime::ResourceRegistry::GetMeshService()->Get( *mesh.MeshHandle );

                                 if ( !targetMesh )
                                     return;

                                 break;
                             }

                             case StaticMeshComponent::Type::None:
                             default:
                                 return;
                         }

                         renderer->AddStaticMesh( targetMesh, mesh.Material, transform.GetTransform() );
                     } );
            }

            /* =========================
               SKINNED MESHES
               ========================= */
            {
                auto view = registry.view<SkinnedMeshComponent, AnimationComponent, TransformComponent>();

                view.each(
                     [&]( entt::entity entity, const SkinnedMeshComponent& mesh,
                          const AnimationComponent& animation, const TransformComponent& transform )
                     {
                         if ( !mesh.Material || !animation.Animator)
                             return;

                         auto baseMesh = Runtime::ResourceRegistry::GetMeshService()->Get( mesh.MeshHandle );

                         if ( !baseMesh || !baseMesh->IsSkinned() )
                             return;

                         auto skinnedMesh = std::static_pointer_cast<Desert::SkinnedMesh>( baseMesh );

                         const Animation::Pose& pose = animation.Animator->GetPose();

                         renderer->AddSkinnedMesh( skinnedMesh, mesh.Material, transform.GetTransform(),
                                                   pose.BoneMatrices );
                     } );
            }
        }
    };
} // namespace Desert::ECS