#include "PickingController.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>

#include <Common/Core/Math/Ray.hpp>

namespace Desert::Editor::Tools
{
    void PickingController::Pick( ::Desert::Core::Scene& scene, const glm::vec2& mouseViewport,
                                  const glm::vec2& viewportSize, bool gizmoHovered, bool additive )
    {
        if ( gizmoHovered )
            return;

        const auto& mainCamera = scene.GetMainCamera().lock();
        if ( !mainCamera )
            return;

        const auto ray = Common::Math::Ray::FromScreenPosition(
             { mouseViewport.x, mouseViewport.y }, mainCamera->GetProjectionMatrix(),
             mainCamera->GetViewMatrix(), mainCamera->GetPosition(),
             static_cast<uint32_t>( viewportSize.x ), static_cast<uint32_t>( viewportSize.y ) );

        // Engine-owned ray cast vs scene meshes (shared with the foliage tool — one raycast, one resolution).
        ::Desert::Core::RaycastHit pick;
        if ( !scene.Raycast( ray, pick ) )
        {
            if ( !additive )
                Core::SelectionManager::ClearSelection(); // click empty space = deselect
            return;
        }

        Common::UUID selectedUUID = pick.Entity;
        auto&        registry     = scene.GetRegistry();

        // If the hit entity is a child of a prefab, select the prefab ROOT so the whole prefab is outlined
        // instead of just one submesh.
        if ( auto hitEntityRef = scene.FindEntityByID( selectedUUID ) )
        {
            const ECS::Entity& hitEntity = hitEntityRef->get();
            if ( !hitEntity.HasComponent<ECS::PrefabComponent>() )
            {
                entt::entity current = hitEntity.GetHandle();
                while ( registry.has<ECS::RelationshipComponent>( current ) )
                {
                    const auto& rel = registry.get<ECS::RelationshipComponent>( current );
                    if ( rel.Parent == entt::null )
                        break;
                    current = rel.Parent;
                    if ( registry.has<ECS::PrefabComponent>( current ) &&
                         registry.has<ECS::UUIDComponent>( current ) )
                    {
                        selectedUUID = registry.get<ECS::UUIDComponent>( current ).UUID;
                        break;
                    }
                }
            }
        }

        if ( additive )
            Core::SelectionManager::Toggle( selectedUUID );
        else
            Core::SelectionManager::SetSelected( selectedUUID );
    }
} // namespace Desert::Editor::Tools
