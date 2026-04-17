#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Animation/Animator.hpp>

#include <Engine/Graphic/Render/Commands/DrawMeshCommand.hpp>
#include <Engine/Graphic/Render/Commands/DrawSkinnedMeshCommand.hpp>

namespace Desert::ECS
{
    class MeshECSSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                     const Common::Timestep& ts ) override
        {
            /* =========================
               STATIC MESHES
               ========================= */
            {
                auto view = registry.view<StaticMeshComponent, TransformComponent>();

                view.each(
                     [&]( entt::entity entity, const StaticMeshComponent& mesh,
                          const TransformComponent& transform )
                     {
                         Desert::Mesh* targetMesh = nullptr;

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
                                 if ( !mesh.MeshHandle || mesh.MaterialSlots.empty() )
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
                         // TODO: avoid reallocating and rebuilding this vector every frame.
                         // Cache resolved material pointers per entity (or per component) and update only when
                         // MaterialSlots change.
                         std::vector<Graphic::Material*> materialSlots;
                         materialSlots.reserve( mesh.MaterialSlots.size() );
                         for ( const auto& assetHandle : mesh.MaterialSlots )
                         {
                             materialSlots.push_back(
                                  Runtime::ResourceRegistry::GetMaterialService()->Get( assetHandle ) );
                         }

                         renderCommandBuffer.Emplace<Graphic::Render::DrawStaticMeshCommand>(
                              targetMesh, materialSlots, transform.GetTransform() );
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
                         if ( mesh.MaterialSlots.empty() || !animation.Animator )
                             return;

                         auto baseMesh = Runtime::ResourceRegistry::GetMeshService()->Get( mesh.MeshHandle );

                         if ( !baseMesh || !baseMesh->IsSkinned() )
                             return;

                         auto skinnedMesh = static_cast<Desert::SkinnedMesh*>( baseMesh );

                         const Animation::Pose& pose = animation.Animator->GetPose();

                         /* renderCommandBuffer.Emplace<Graphic::Render::DrawSkinnedMeshCommand>(
                               skinnedMesh, mesh.Material, transform.GetTransform(), pose.BoneMatrices );*/
                     } );
            }
        }
    };
} // namespace Desert::ECS