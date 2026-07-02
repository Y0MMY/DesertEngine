#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Animation/Animator.hpp>

#include <Engine/Graphic/Render/Commands/DrawMeshCommand.hpp>
#include <Engine/Graphic/Render/Commands/DrawSkinnedMeshCommand.hpp>
#include <Engine/Graphic/Render/Commands/DrawGenericMeshCommand.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Animation/Skeleton.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/SkinnedMaterialPBR.hpp>

#include <Engine/Runtime/SelectionContext.hpp>

namespace Desert::ECS
{
    class MeshECSSystem : public System
    {
    public:
        explicit MeshECSSystem() : System()
        {
            m_DefaultMaterial        = std::make_shared<Graphic::StaticMaterialPBR>();
            m_DefaultSkinnedMaterial = std::make_shared<Graphic::SkinnedMaterialPBR>();
        }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                     const Common::Timestep& ts ) override
        {
            /* =========================
               STATIC MESHES
               ========================= */
            {
                auto view = registry.view<StaticMeshComponent, TransformComponent>();

                // Frame-constant: read the current selection ONCE, not per entity (256x/frame otherwise).
                const auto selectedUUID = Runtime::SelectionContext::Get();

                view.each(
                     [&]( entt::entity entity, StaticMeshComponent& mesh,
                          const TransformComponent& transform )
                     {
                         // Hidden entities (Visible toggle) are skipped.
                         if ( registry.has<VisibilityComponent>( entity ) &&
                              !registry.get<VisibilityComponent>( entity ).Visible )
                             return;

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
                             // Use the process-wide SHARED primitive mesh (one Mesh* per type) so identical
                             // primitives batch via instancing. NOT stored in RuntimeMesh — leaving it null
                             // keeps the entity on the shared mesh; the mesh editor forks a per-entity
                             // RuntimeMesh on edit (copy-on-edit), which then takes priority above.
                             targetMesh = Geometry::PrimitiveMeshFactory::GetShared( mesh.Primitive.value() );
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

                             // Rebuild the raw-pointer slot view ONLY here (when the instance set changes), not
                             // every frame: the render command/queue takes a pointer to this stable vector, so
                             // a per-frame allocate+copy of a slot vector per entity is eliminated.
                             mesh.RuntimeSlotPtrs.clear();
                             mesh.RuntimeSlotPtrs.reserve( mesh.RuntimeMaterialInstances.size() );
                             for ( const auto& inst : mesh.RuntimeMaterialInstances )
                                 mesh.RuntimeSlotPtrs.push_back( inst.get() );

                             // Tint/roughen a batched PBR primitive straight from a MaterialComponent (no
                             // material asset needed) — e.g. the Cornell Box GI test colours its walls this
                             // way. Applied when instances are (re)built; the override persists and
                             // BuildEffectiveMaterial honours it. Empty/"StaticMeshPBR" shader = still PBR.
                             if ( registry.has<MaterialComponent>( entity ) && !mesh.RuntimeMaterialInstances.empty() )
                             {
                                 const auto& matc = registry.get<MaterialComponent>( entity );
                                 if ( matc.ShaderName.empty() || matc.ShaderName == "StaticMeshPBR" )
                                 {
                                     auto& inst = mesh.RuntimeMaterialInstances[0];
                                     for ( const auto& p : matc.Params )
                                     {
                                         if ( p.Name == "AlbedoColor" )
                                             inst->SetVec4( "AlbedoColor", p.Value );
                                         else if ( p.Name == "MetallicFactor" )
                                             inst->SetFloat( "MetallicFactor", p.Value.x );
                                         else if ( p.Name == "RoughnessFactor" )
                                             inst->SetFloat( "RoughnessFactor", p.Value.x );
                                         else if ( p.Name == "EmissiveColor" )
                                             inst->SetVec4( "EmissiveColor", p.Value );
                                         else if ( p.Name == "EmissiveIntensity" )
                                             inst->SetFloat( "EmissiveIntensity", p.Value.x );
                                         else if ( p.Name == "Transmission" )
                                             inst->SetFloat( "Transmission", p.Value.x );
                                         else if ( p.Name == "IOR" )
                                             inst->SetFloat( "IOR", p.Value.x );
                                         else if ( p.Name == "GlassTint" )
                                             inst->SetVec4( "GlassTint", p.Value );
                                     }
                                 }
                             }
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
                         if ( const auto& selected = selectedUUID )
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

                         // A MaterialComponent assigning a NON-PBR shader takes this mesh off the batched
                         // PBR path onto the generic per-object data-driven path.
                         if ( registry.has<MaterialComponent>( entity ) )
                         {
                             const auto& matc = registry.get<MaterialComponent>( entity );
                             if ( !matc.ShaderName.empty() && matc.ShaderName != "StaticMeshPBR" &&
                                  matc.ShaderName != "SkinnedMeshPBR" )
                             {
                                 std::vector<std::pair<std::string, glm::vec4>> overrides;
                                 overrides.reserve( matc.Params.size() );
                                 for ( const auto& p : matc.Params )
                                     overrides.emplace_back( p.Name, p.Value );

                                 std::vector<std::pair<std::string, uint64_t>> texOverrides;
                                 texOverrides.reserve( matc.Textures.size() );
                                 for ( const auto& t : matc.Textures )
                                     texOverrides.emplace_back( t.Name, t.TextureHandle );

                                 renderCommandBuffer.Emplace<Graphic::Render::DrawGenericMeshCommand>(
                                      targetMesh, worldTransform, matc.ShaderName,
                                      Graphic::MaterialOverrides{ std::move( overrides ),
                                                                  std::move( texOverrides ) },
                                      isSelected );
                                 return; // skip the PBR path for this entity
                             }
                         }

                         renderCommandBuffer.Emplace<Graphic::Render::DrawStaticMeshCommand>(
                              targetMesh, &mesh.RuntimeSlotPtrs, worldTransform, isSelected,
                              mesh.HiddenSubmeshes );
                     } );
            }

            /* =========================
               INSTANCED STATIC MESHES (UE-style ISM: one entity = N instances, one instanced draw)
               ========================= */
            {
                auto view = registry.view<InstancedStaticMeshComponent>();
                view.each(
                     [&]( entt::entity entity, InstancedStaticMeshComponent& ism )
                     {
                         if ( registry.has<VisibilityComponent>( entity ) &&
                              !registry.get<VisibilityComponent>( entity ).Visible )
                             return;
                         if ( ism.InstanceTransforms.empty() )
                             return;

                         // Resolve the single shared mesh (edited RuntimeMesh > primitive > asset handle).
                         Desert::Mesh* targetMesh = nullptr;
                         if ( ism.RuntimeMesh )
                             targetMesh = ism.RuntimeMesh.get();
                         else if ( ism.Primitive.has_value() )
                             targetMesh = Geometry::PrimitiveMeshFactory::GetShared( ism.Primitive.value() );
                         else if ( ism.MeshHandle )
                             targetMesh = Runtime::ResourceRegistry::GetMeshService()->Get( ism.MeshHandle );
                         if ( !targetMesh )
                             return;

                         // One PBR material instance (slot 0), rebuilt only when the slot set changes.
                         const size_t slotCount = ism.MaterialSlots.empty() ? 1 : ism.MaterialSlots.size();
                         if ( ism.RuntimeMaterialInstances.size() != slotCount )
                         {
                             ism.RuntimeMaterialInstances.clear();
                             if ( ism.MaterialSlots.empty() )
                                 ism.RuntimeMaterialInstances.push_back( m_DefaultMaterial->CreateInstance() );
                             else
                                 for ( const auto& h : ism.MaterialSlots )
                                 {
                                     auto* base = Runtime::ResourceRegistry::GetMaterialService()->Get( h );
                                     ism.RuntimeMaterialInstances.push_back(
                                          base ? base->CreateInstance() : m_DefaultMaterial->CreateInstance() );
                                 }
                         }
                         if ( ism.RuntimeMaterialInstances.empty() )
                             return;

                         // InstanceTransforms are WORLD-space (the entity is a container); zero-copy pointer.
                         renderCommandBuffer.Emplace<Graphic::Render::DrawInstancedStaticMeshCommand>(
                              targetMesh, ism.RuntimeMaterialInstances[0].get(), &ism.InstanceTransforms );
                     } );
            }

            /* =========================
               SKINNED MESHES
               ========================= */
            {
                // AnimationComponent is OPTIONAL: a skinned mesh with no animator renders in its BIND pose
                // (identity bone matrices) — so imported/rigged characters show up in the skeleton editor.
                auto view = registry.view<SkinnedMeshComponent, TransformComponent>();

                view.each(
                     [&]( entt::entity entity, SkinnedMeshComponent& mesh, const TransformComponent& transform )
                     {
                         if ( registry.has<VisibilityComponent>( entity ) &&
                              !registry.get<VisibilityComponent>( entity ).Visible )
                             return;

                         auto baseMesh = Runtime::ResourceRegistry::GetMeshService()->Get( mesh.MeshHandle );
                         if ( !baseMesh || !baseMesh->IsSkinned() )
                             return;
                         auto* skinnedMesh = static_cast<Desert::SkinnedMesh*>( baseMesh );

                         // One skinned PBR material instance (default if no slot assigned), rebuilt only when
                         // the slot set changes.
                         const size_t slotCount = mesh.MaterialSlots.empty() ? 1 : mesh.MaterialSlots.size();
                         if ( mesh.RuntimeMaterialInstances.size() != slotCount )
                         {
                             mesh.RuntimeMaterialInstances.clear();
                             if ( mesh.MaterialSlots.empty() )
                                 mesh.RuntimeMaterialInstances.push_back(
                                      m_DefaultSkinnedMaterial->CreateInstance() );
                             else
                                 for ( const auto& h : mesh.MaterialSlots )
                                 {
                                     auto* base = Runtime::ResourceRegistry::GetMaterialService()->Get( h );
                                     mesh.RuntimeMaterialInstances.push_back(
                                          base ? base->CreateInstance()
                                               : m_DefaultSkinnedMaterial->CreateInstance() );
                                 }
                         }
                         if ( mesh.RuntimeMaterialInstances.empty() )
                             return;

                         std::vector<Graphic::MaterialInstance*> slots;
                         slots.reserve( mesh.RuntimeMaterialInstances.size() );
                         for ( const auto& inst : mesh.RuntimeMaterialInstances )
                             slots.push_back( inst.get() );

                         // Bone matrices: animated pose if an Animator exists, else bind pose (identity = the
                         // skeleton's rest shape, which stays correct after Phase-2 rest-pose edits since
                         // OffsetMatrix is recomputed alongside LocalBindTransform).
                         std::vector<glm::mat4> boneMatrices;
                         if ( registry.has<AnimationComponent>( entity ) )
                         {
                             const auto& anim = registry.get<AnimationComponent>( entity );
                             if ( anim.Animator )
                                 boneMatrices = anim.Animator->GetPose().BoneMatrices;
                         }
                         if ( boneMatrices.empty() )
                         {
                             // Proper BIND pose: bind matrix = chainGlobal * OffsetMatrix (NOT identity).
                             // Identity would render the RAW (unscaled, thousands-of-units) vertices; the
                             // chainGlobal*OffsetMatrix bind renders the mesh at its authored size and makes
                             // it line up with the bone overlay (which is drawn at chainGlobal).
                             const auto& bones = skinnedMesh->GetSkeleton().GetBones();
                             std::vector<glm::mat4>             g( bones.size(), glm::mat4( 1.0f ) );
                             std::vector<bool>                  done( bones.size(), false );
                             std::function<glm::mat4( size_t )> resolve = [&]( size_t i ) -> glm::mat4
                             {
                                 if ( done[i] )
                                     return g[i];
                                 glm::mat4 m = bones[i].LocalBindTransform;
                                 if ( bones[i].ParentBoneID.has_value() &&
                                      bones[i].ParentBoneID.value() < bones.size() )
                                     m = resolve( bones[i].ParentBoneID.value() ) * bones[i].LocalBindTransform;
                                 g[i]    = m;
                                 done[i] = true;
                                 return m;
                             };
                             boneMatrices.resize( bones.size() );
                             for ( size_t i = 0; i < bones.size(); ++i )
                                 boneMatrices[i] = resolve( i ) * bones[i].OffsetMatrix;
                         }

                         bool isSelected = false;
                         if ( auto selected = Runtime::SelectionContext::Get();
                              selected.has_value() && registry.has<UUIDComponent>( entity ) )
                             isSelected = ( registry.get<UUIDComponent>( entity ).UUID == *selected );

                         // WORLD transform (walk the parent chain) — a skinned mesh parented to e.g. the
                         // character controller must follow it; using the local transform left it behind at
                         // the origin while the (parent-aware) camera moved away. Mirrors the static path.
                         glm::mat4    worldTransform = transform.GetTransform();
                         entt::entity current        = entity;
                         while ( registry.has<RelationshipComponent>( current ) )
                         {
                             const auto& rel = registry.get<RelationshipComponent>( current );
                             if ( rel.Parent == entt::null )
                                 break;
                             current = rel.Parent;
                             if ( registry.has<TransformComponent>( current ) )
                                 worldTransform =
                                      registry.get<TransformComponent>( current ).GetTransform() * worldTransform;
                         }

                         renderCommandBuffer.Emplace<Graphic::Render::DrawSkinnedMeshCommand>(
                              skinnedMesh, slots, worldTransform, boneMatrices, isSelected );
                     } );
            }
        }

    private:
        std::shared_ptr<Graphic::StaticMaterialPBR>  m_DefaultMaterial;
        std::shared_ptr<Graphic::SkinnedMaterialPBR> m_DefaultSkinnedMaterial;
    };
} // namespace Desert::ECS