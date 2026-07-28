#include "GizmoController.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Selection/SkeletonEditMode.hpp>
#include <Editor/Core/Commands/SceneCommands.hpp>
#include <Editor/Panels/MeshEditor/MeshEditorPanel.hpp>

#include <ImGui/imgui.h>

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
        auto& reg                = scene.GetRegistry();

        // The gizmo must work in WORLD space. For a CHILD entity (e.g. a camera parented to the character),
        // the world transform = parentWorld * local, and an edit must be converted back to LOCAL before
        // writing. parentWorld = identity for a root entity (so this is a no-op there).
        auto parentWorldOf = [&reg]( entt::entity e ) -> glm::mat4
        {
            glm::mat4 world( 1.0f );
            if ( reg.has<ECS::RelationshipComponent>( e ) )
            {
                std::vector<entt::entity> chain; // [parent, grandparent, ... root]
                entt::entity              cur = reg.get<ECS::RelationshipComponent>( e ).Parent;
                while ( cur != entt::null )
                {
                    chain.push_back( cur );
                    cur = reg.has<ECS::RelationshipComponent>( cur )
                               ? reg.get<ECS::RelationshipComponent>( cur ).Parent
                               : entt::null;
                }
                for ( auto it = chain.rbegin(); it != chain.rend(); ++it ) // root -> ... -> parent
                    if ( reg.has<ECS::TransformComponent>( *it ) )
                        world = world * reg.get<ECS::TransformComponent>( *it ).GetTransform();
            }
            return world;
        };

        // An entity with a selected ANCESTOR is carried by that ancestor's transform already — the group
        // logic must skip it (else it would move twice).
        auto coveredBySelection = [&reg]( entt::entity e )
        {
            entt::entity cur = e;
            while ( reg.has<ECS::RelationshipComponent>( cur ) )
            {
                const auto parent = reg.get<ECS::RelationshipComponent>( cur ).Parent;
                if ( parent == entt::null )
                    break;
                cur = parent;
                if ( reg.has<ECS::UUIDComponent>( cur ) &&
                     Core::SelectionManager::IsSelected( reg.get<ECS::UUIDComponent>( cur ).UUID ) )
                    return true;
            }
            return false;
        };

        const glm::mat4 parentWorld = parentWorldOf( selectedEntity.GetHandle() );

        auto            modelMatrix    = parentWorld * transformComponent.GetTransform(); // world, for the gizmo
        const glm::mat4 oldModelMatrix = modelMatrix;

        // SetRect MUST match the rendered scene-image rect (content region), NOT the raw window rect.
        ImGuizmo::SetOrthographic( false );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y );

        const auto& view = mainCamera->GetViewMatrix();
        const auto& proj = mainCamera->GetProjectionMatrix();

        // Snap: grid for translate, fixed angles for rotate, increments for scale. Active when the
        // toolbar's magnet toggle is on OR Ctrl is held (Ctrl inverts the toggle).
        const auto  operation     = Core::GizmoState::Get();
        float       snapValues[3] = { 0.0f, 0.0f, 0.0f };
        const float snapUnit      = ( operation == Core::GizmoState::Operation::Rotate )
                                         ? Core::GizmoState::RotateSnapDegrees()
                                         : ( operation == Core::GizmoState::Operation::Scale )
                                              ? Core::GizmoState::ScaleSnap()
                                              : Core::GizmoState::TranslateSnap();
        snapValues[0] = snapValues[1] = snapValues[2] = snapUnit;
        const float* snap =
             Core::GizmoState::SnapActive( ::ImGui::GetIO().KeyCtrl ) ? snapValues : nullptr;

        const bool manipulated =
             ImGuizmo::Manipulate( &view[0][0], &proj[0][0], static_cast<ImGuizmo::OPERATION>( operation ),
                                   ImGuizmo::WORLD, &modelMatrix[0][0], nullptr, snap );

        // Picking must stand down whenever the cursor is OVER the gizmo — not only mid-drag. Setting this
        // only while manipulating meant a first click on a gizmo axis drawn over another mesh SELECTED
        // that mesh instead of starting the manipulation.
        if ( ImGuizmo::IsOver() || ImGuizmo::IsUsing() )
            m_Hovered = true;

        // One undo entry per drag: when the drag STARTS this frame, capture the pre-drag TRS of every
        // selected top-level root NOW — before any of this frame's deltas are written below.
        const bool usingNow = ImGuizmo::IsUsing();
        if ( usingNow && !m_DragActive )
        {
            m_DragActive = true;
            m_DragEntity = *selected;
            m_DragSnapshots.clear();
            for ( const auto& id : Core::SelectionManager::GetSelection() )
            {
                auto ref = scene.FindEntityByID( id );
                if ( !ref )
                    continue;
                ECS::Entity e = ref->get();
                if ( !e.HasComponent<ECS::TransformComponent>() || coveredBySelection( e.GetHandle() ) )
                    continue;
                const auto& tc = e.GetComponent<ECS::TransformComponent>();
                m_DragSnapshots.push_back( { id, tc.Translation, tc.Rotation, tc.Scale } );
            }
        }

        if ( manipulated )
        {
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

            // Group manipulation: apply the same WORLD-space delta to every other selected top-level root,
            // so the whole selection moves/rotates/scales as one rigid group around the primary's gizmo.
            const auto& allSelected = Core::SelectionManager::GetSelection();
            if ( allSelected.size() > 1 )
            {
                const glm::mat4 delta = modelMatrix * glm::inverse( oldModelMatrix );
                for ( const auto& id : allSelected )
                {
                    if ( id == *selected )
                        continue;
                    auto ref = scene.FindEntityByID( id );
                    if ( !ref )
                        continue;
                    ECS::Entity e = ref->get();
                    if ( !e.HasComponent<ECS::TransformComponent>() || coveredBySelection( e.GetHandle() ) )
                        continue;

                    auto&           tc       = e.GetComponent<ECS::TransformComponent>();
                    const glm::mat4 pw       = parentWorldOf( e.GetHandle() );
                    const glm::mat4 newLocal = glm::inverse( pw ) * ( delta * ( pw * tc.GetTransform() ) );

                    glm::vec3 s, t, sk;
                    glm::quat r;
                    glm::vec4 persp;
                    glm::decompose( newLocal, s, r, t, sk, persp );
                    tc.Translation = t;
                    tc.Rotation    = glm::eulerAngles( r );
                    tc.Scale       = s;
                }
            }
        }

        // Commit old->current for the whole group on release. The UUID guard drops the pending capture if
        // the selection changed mid-drag.
        if ( !usingNow && m_DragActive )
        {
            m_DragActive = false;
            if ( m_DragEntity == *selected )
                Commands::RecordTransformEdits( m_DragSnapshots );
            m_DragSnapshots.clear();
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
        const auto op =
             ( Core::GizmoState::Get() == Operation::Rotate ) ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;

        const bool boneManipulated =
             ImGuizmo::Manipulate( &view[0][0], &proj[0][0], op, ImGuizmo::WORLD, &gizmoWorld[0][0] );

        // Same rule as the object gizmo: picking stands down on HOVER, not only mid-drag.
        if ( ImGuizmo::IsOver() || ImGuizmo::IsUsing() )
            m_Hovered = true;

        if ( boneManipulated )
        {
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
