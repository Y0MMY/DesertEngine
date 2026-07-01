#include "GizmoController.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Selection/SkeletonEditMode.hpp>
#include <Editor/Panels/MeshEditor/MeshEditorPanel.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Animation/Skeleton.hpp>

#include <ImGuizmo.h>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <vector>

namespace Desert::Editor::Tools
{
    void GizmoController::RenderObject( ::Desert::Core::Scene& scene, const glm::vec2& viewportPos,
                                        const glm::vec2& viewportSize )
    {
        // NOTE: ImGuizmo::BeginFrame() is issued once per frame by EditorLayer, before any panel runs.
        const auto& mainCamera = scene.GetMainCamera().lock();
        if ( !mainCamera )
            return;

        const auto& selected = Core::SelectionManager::GetSelected();
        if ( !selected )
            return;

        const auto& selectedEntityOpt = scene.FindEntityByID( *selected );
        if ( !selectedEntityOpt )
            return;

        auto& selectedEntity = selectedEntityOpt->get();

        // If the Mesh Editor is open and editing this entity, vertex editing owns the (global) ImGuizmo
        // interaction — drawing the object gizmo here would steal the drag and move the whole object.
        if ( auto* meshEditor = MeshEditorPanel::GetInstance();
             meshEditor && meshEditor->IsActivelyEditing( selectedEntity ) )
            return;

        auto& transformComponent = selectedEntity.GetComponent<ECS::TransformComponent>();

        // The gizmo must work in WORLD space. For a CHILD entity (e.g. a camera parented to the character),
        // the world transform = parentWorld * local, and an edit must be converted back to LOCAL before
        // writing. parentWorld = identity for a root entity (so this is a no-op there).
        glm::mat4  parentWorld( 1.0f );
        auto&      reg  = scene.GetRegistry();
        const auto self = selectedEntity.GetHandle();
        if ( reg.has<ECS::RelationshipComponent>( self ) )
        {
            std::vector<entt::entity> chain; // [parent, grandparent, ... root]
            entt::entity              cur = reg.get<ECS::RelationshipComponent>( self ).Parent;
            while ( cur != entt::null )
            {
                chain.push_back( cur );
                cur = reg.has<ECS::RelationshipComponent>( cur ) ? reg.get<ECS::RelationshipComponent>( cur ).Parent
                                                                 : entt::null;
            }
            for ( auto it = chain.rbegin(); it != chain.rend(); ++it ) // root -> ... -> parent
                if ( reg.has<ECS::TransformComponent>( *it ) )
                    parentWorld = parentWorld * reg.get<ECS::TransformComponent>( *it ).GetTransform();
        }

        auto modelMatrix = parentWorld * transformComponent.GetTransform(); // world transform for the gizmo

        // SetRect MUST match the rendered scene-image rect (content region), NOT the raw window rect.
        ImGuizmo::SetOrthographic( false );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y );

        const auto& view = mainCamera->GetViewMatrix();
        const auto& proj = mainCamera->GetProjectionMatrix();

        if ( ImGuizmo::Manipulate( &view[0][0], &proj[0][0], static_cast<ImGuizmo::OPERATION>( m_Op ),
                                   ImGuizmo::WORLD, &modelMatrix[0][0] ) )
        {
            if ( ImGuizmo::IsOver() )
                m_Hovered = true;

            // Convert the manipulated WORLD matrix back to the entity's LOCAL space (inverse parent) before
            // decomposing — so dragging a child entity edits its local offset correctly.
            const glm::mat4 localMatrix = glm::inverse( parentWorld ) * modelMatrix;

            glm::vec3 scale, translation, skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose( localMatrix, scale, rotation, translation, skew, perspective );

            transformComponent.Translation = translation;
            transformComponent.Rotation    = glm::eulerAngles( rotation );
            transformComponent.Scale       = scale;
        }
    }

    void GizmoController::RenderBone( ::Desert::Core::Scene& scene, const glm::vec2& viewportPos,
                                     const glm::vec2& viewportSize )
    {
        const auto& mainCamera = scene.GetMainCamera().lock();
        if ( !mainCamera )
            return;

        const int boneIdx = Core::SkeletonEditMode::GetSelectedBone();
        if ( boneIdx < 0 )
            return;

        const auto& selected = Core::SelectionManager::GetSelected();
        if ( !selected )
            return;
        const auto& entOpt = scene.FindEntityByID( *selected );
        if ( !entOpt )
            return;
        auto& entity = entOpt->get();
        if ( !entity.HasComponent<ECS::SkinnedMeshComponent>() )
            return;

        auto& smc  = entity.GetComponent<ECS::SkinnedMeshComponent>();
        auto* mesh = Runtime::ResourceRegistry::GetMeshService()->Get( smc.MeshHandle );
        if ( !mesh || !mesh->IsSkinned() )
            return;
        auto* skeleton = static_cast<SkinnedMesh*>( mesh )->GetSkeletonMutable();
        auto& bones    = skeleton->GetBonesMutable();
        if ( boneIdx >= static_cast<int>( bones.size() ) )
            return;

        const glm::mat4 entityWorld = entity.GetComponent<ECS::TransformComponent>().GetTransform();

        // Chain global bind per bone — the SAME space the mesh is skinned in, so the gizmo sits on the bone.
        std::vector<glm::mat4>             chainGlobal( bones.size(), glm::mat4( 1.0f ) );
        std::vector<bool>                  done( bones.size(), false );
        std::function<glm::mat4( size_t )> resolve = [&]( size_t i ) -> glm::mat4
        {
            if ( done[i] )
                return chainGlobal[i];
            glm::mat4 g = bones[i].LocalBindTransform;
            if ( bones[i].ParentBoneID.has_value() && bones[i].ParentBoneID.value() < bones.size() )
                g = resolve( bones[i].ParentBoneID.value() ) * bones[i].LocalBindTransform;
            chainGlobal[i] = g;
            done[i]        = true;
            return g;
        };

        glm::mat4 gizmoWorld = entityWorld * resolve( static_cast<size_t>( boneIdx ) );

        ImGuizmo::SetOrthographic( false );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y );

        const auto& view = mainCamera->GetViewMatrix();
        const auto& proj = mainCamera->GetProjectionMatrix();
        // Scale on a bone's rest pose is rarely wanted; default None/Scale to Translate.
        const auto op = ( m_Op == Operation::Rotate ) ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;

        if ( ImGuizmo::Manipulate( &view[0][0], &proj[0][0], op, ImGuizmo::WORLD, &gizmoWorld[0][0] ) )
        {
            m_Hovered = ImGuizmo::IsOver();

            // Edit ONLY this bone's LocalBindTransform (relative to its parent's unchanged chain global).
            const glm::mat4 newGlobalMesh = glm::inverse( entityWorld ) * gizmoWorld;
            glm::mat4       parentGlobal( 1.0f );
            if ( bones[boneIdx].ParentBoneID.has_value() &&
                 bones[boneIdx].ParentBoneID.value() < bones.size() )
                parentGlobal = resolve( bones[boneIdx].ParentBoneID.value() );
            bones[boneIdx].LocalBindTransform = glm::inverse( parentGlobal ) * newGlobalMesh;
        }
    }
} // namespace Desert::Editor::Tools
