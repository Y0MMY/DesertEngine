#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
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
                     [&]( entt::entity entity, StaticMeshComponent& mesh,
                          const TransformComponent& transform )
                     {
                         if ( !mesh.MeshHandle || mesh.MaterialSlots.empty() )
                             return;

                         Desert::Mesh* targetMesh =
                              Runtime::ResourceRegistry::GetMeshService()->Get( mesh.MeshHandle );

                         if ( !targetMesh )
                             return;

                         // Ensure runtime material instances are initialized and match the slots
                         if ( mesh.RuntimeMaterialInstances.size() != mesh.MaterialSlots.size() )
                         {
                             mesh.RuntimeMaterialInstances.clear();
                             mesh.RuntimeMaterialInstances.reserve( mesh.MaterialSlots.size() );

                             for ( const auto& assetHandle : mesh.MaterialSlots )
                             {
                                 auto* baseMaterial = Runtime::ResourceRegistry::GetMaterialService()->Get( assetHandle );
                                 if ( baseMaterial )
                                 {
                                     mesh.RuntimeMaterialInstances.push_back( baseMaterial->CreateInstance() );
                                 }
                             }
                         }

                         // Prepare raw pointers for the render command (the instances are kept alive by the component)
                         std::vector<Graphic::MaterialInstance*> materialSlots;
                         materialSlots.reserve( mesh.RuntimeMaterialInstances.size() );
                         for ( const auto& inst : mesh.RuntimeMaterialInstances )
                         {
                             materialSlots.push_back( inst.get() );
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