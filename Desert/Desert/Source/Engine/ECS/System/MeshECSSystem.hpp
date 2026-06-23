#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Animation/Animator.hpp>

#include <Engine/Graphic/Render/Commands/DrawMeshCommand.hpp>
#include <Engine/Graphic/Render/Commands/DrawSkinnedMeshCommand.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>

#include <Editor/Core/Selection/SelectionManager.hpp>

namespace Desert::ECS
{
    class MeshECSSystem : public System
    {
    public:
        explicit MeshECSSystem() : System()
        {
            m_DefaultMaterial = std::make_shared<Graphic::StaticMaterialPBR>();
        }

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
                         if ( !mesh.RuntimeMesh && !mesh.Primitive.has_value() && !mesh.MeshHandle )
                             return;

                         Desert::Mesh* targetMesh = nullptr;

                         if ( mesh.RuntimeMesh )
                         {
                             // Use unique modified mesh
                             targetMesh = mesh.RuntimeMesh.get();
                         }
                         else if ( mesh.Primitive.has_value() )
                         {
                             // Dynamic primitive generation (Cache in RuntimeMesh to avoid per-frame buffer creation)
                             mesh.RuntimeMesh = Geometry::PrimitiveMeshFactory::Create( mesh.Primitive.value() );
                             if ( mesh.RuntimeMesh )
                             {
                                 mesh.RuntimeMesh->Invalidate();
                                 targetMesh = mesh.RuntimeMesh.get();
                             }
                         }
                         else if ( mesh.MeshHandle )
                         {
                             // Asset-based mesh
                             targetMesh = Runtime::ResourceRegistry::GetMeshService()->Get( mesh.MeshHandle );
                         }

                         if ( !targetMesh )
                             return;

                         // --- Auto-Initialize Material Slots ---
                         // If the component has no materials assigned, try to fetch defaults from the asset
                         if ( mesh.MaterialSlots.empty() && mesh.MeshHandle )
                         {
                             auto* meshAsset = Runtime::ResourceRegistry::GetMeshService()->GetAsset( mesh.MeshHandle );
                             if ( meshAsset )
                             {
                                 const auto& defaultHandles = meshAsset->GetMaterialHandles();
                                 for ( const auto& h : defaultHandles )
                                 {
                                     // Resolve external handle to internal asset handle
                                     mesh.MaterialSlots.push_back( 
                                         Runtime::ResourceRegistry::GetMaterialService()->GetAssetHandleByExternal( h ) 
                                     );
                                 }
                             }
                         }

                         // Ensure runtime material instances are initialized and match the slots
                         size_t slotCount = mesh.MaterialSlots.empty() ? 1 : mesh.MaterialSlots.size();

                         if ( mesh.RuntimeMaterialInstances.size() != slotCount )
                         {
                             mesh.RuntimeMaterialInstances.clear();
                             mesh.RuntimeMaterialInstances.reserve( slotCount );

                             if ( mesh.MaterialSlots.empty() )
                             {
                                 // Use persistent system default material template
                                 mesh.RuntimeMaterialInstances.push_back( m_DefaultMaterial->CreateInstance() );
                             }
                             else
                             {
                                 for ( const auto& assetHandle : mesh.MaterialSlots )
                                 {
                                     auto* baseMaterial = Runtime::ResourceRegistry::GetMaterialService()->Get( assetHandle );
                                     if ( baseMaterial )
                                     {
                                         mesh.RuntimeMaterialInstances.push_back( baseMaterial->CreateInstance() );
                                     }
                                     else
                                     {
                                         // Fallback to system default if asset is not loaded
                                         mesh.RuntimeMaterialInstances.push_back( m_DefaultMaterial->CreateInstance() );
                                     }
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

                         glm::mat4 worldTransform = transform.GetTransform();

                         entt::entity current = entity;
                         while ( registry.has<RelationshipComponent>( current ) )
                         {
                             const auto& rel = registry.get<RelationshipComponent>( current );
                             if ( rel.Parent == entt::null ) break;

                             current = rel.Parent;
                             if ( registry.has<TransformComponent>( current ) )
                             {
                                 const auto& parentTransform = registry.get<TransformComponent>( current );
                                 worldTransform = parentTransform.GetTransform() * worldTransform;
                             }
                         }

                         bool isSelected = false;
                         if ( auto selected = Editor::Core::SelectionManager::GetSelected() )
                         {
                             // Check this entity itself
                             if ( registry.has<UUIDComponent>( entity ) &&
                                  registry.get<UUIDComponent>( entity ).UUID == *selected )
                             {
                                 isSelected = true;
                             }
                             else
                             {
                                 // Walk ancestor chain — selecting a prefab root outlines all its mesh children
                                 entt::entity ancestor = entity;
                                 while ( registry.has<RelationshipComponent>( ancestor ) )
                                 {
                                     const auto& rel = registry.get<RelationshipComponent>( ancestor );
                                     if ( rel.Parent == entt::null )
                                         break;
                                     ancestor = rel.Parent;
                                     if ( registry.has<UUIDComponent>( ancestor ) &&
                                          registry.get<UUIDComponent>( ancestor ).UUID == *selected )
                                     {
                                         isSelected = true;
                                         break;
                                     }
                                 }
                             }
                         }

                         renderCommandBuffer.Emplace<Graphic::Render::DrawStaticMeshCommand>(
                              targetMesh, materialSlots, worldTransform, isSelected );
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

    private:
        std::shared_ptr<Graphic::StaticMaterialPBR> m_DefaultMaterial;
    };
} // namespace Desert::ECS