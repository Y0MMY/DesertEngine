#include "ViewportPanel.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Panels/MeshEditor/MeshEditorPanel.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>

#include <ImGuizmo.h>
#include <glm/gtx/matrix_decompose.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;
    ViewportPanel::ViewportPanel( const std::shared_ptr<Desert::Core::Scene>& scene )
         : IPanel( "Scene###scene" ), m_Scene( scene )
    {
        m_UIHelper = std::make_unique<Editor::UI::UIHelper>();
        m_UIHelper->Init();

        m_LightGizmoRenderer = std::make_unique<LightGizmoRenderer>( scene );
    }

    void ViewportPanel::OnUIRender()
    {
        const auto& mainCamera = m_Scene->GetMainCamera().lock();
        if ( !mainCamera )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ), "Camera was not found" );
            ImGui::TextWrapped( "Please add a camera to the scene to display the view." );
            return;
        }

        ImVec2 mousePos    = ::ImGui::GetMousePos();
        ImVec2 viewportPos = ::ImGui::GetWindowPos();

        ImVec2 viewportMin = ImGui::GetWindowPos();
        viewportMin.x += ImGui::GetWindowContentRegionMin().x;
        viewportMin.y += ImGui::GetWindowContentRegionMin().y;

        ImVec2 viewportMax = ImGui::GetWindowPos();
        viewportMax.x += ImGui::GetWindowContentRegionMax().x;
        viewportMax.y += ImGui::GetWindowContentRegionMax().y;

        m_ViewportData.ViewportPos = { ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x,
                                       ImGui::GetWindowPos().y + ImGui::GetWindowContentRegionMin().y };

        m_ViewportData.MousePosition = glm::vec2( mousePos.x - viewportMin.x, mousePos.y - viewportMin.y );
        const auto oldSize           = m_ViewportData.Size;

        m_ViewportData.Size      = { viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y };
        m_ViewportData.IsHovered = ::ImGui::IsWindowHovered();

        if ( oldSize != m_ViewportData.Size )
        {
            // Store the new size and apply it in OnPreUpdate() next frame, before any recording
            // starts. Calling Scene::Resize() here (inside OnImGuiRender) destroys descriptor set
            // pools while their DS are still bound to the recording command buffer.
            m_PendingViewportSize = m_ViewportData.Size;
            mainCamera->UpdateProjectionMatrix( m_ViewportData.Size.x,
                                                m_ViewportData.Size.y ); // TODO: Move to scene
        }

        m_ViewportData.IsHovered = ImGui::IsWindowHovered();

        // Render scene
        m_UIHelper->Image( m_Scene->GetFinalImage(), { m_ViewportData.Size.x, m_ViewportData.Size.y } );

        // Handle gizmos
        m_GizmoHovered = false;
        if ( m_GizmoType != GizmoType::None )
        {
            RenderGizmo();
        }

        m_LightGizmoRenderer->Render( m_ViewportData.Size.x, m_ViewportData.Size.y, m_ViewportData.ViewportPos.x,
                                      m_ViewportData.ViewportPos.y );
    }

    void ViewportPanel::OnPreUpdate()
    {
        if ( m_PendingViewportSize.has_value() )
        {
            m_Scene->Resize( (uint32_t)m_PendingViewportSize->x, (uint32_t)m_PendingViewportSize->y );
            m_PendingViewportSize.reset();
        }
    }

    void ViewportPanel::RenderGizmo()
    {
        ImGuizmo::BeginFrame();

        const auto& mainCamera = m_Scene->GetMainCamera().lock();
        if ( !mainCamera )
            return;

        const auto& selected = Core::SelectionManager::GetSelected();
        if ( !selected )
            return;

        const auto& selectedEntityOpt = m_Scene->FindEntityByID( *selected );
        if ( !selectedEntityOpt )
            return;

        auto& selectedEntity     = selectedEntityOpt->get();
        auto& transformComponent = selectedEntity.GetComponent<ECS::TransformComponent>();
        auto  modelMatrix        = transformComponent.GetTransform();

        float rw = static_cast<float>( ImGui::GetWindowWidth() );
        float rh = static_cast<float>( ImGui::GetWindowHeight() );

        ImGuizmo::SetOrthographic( false );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, rw, rh );

        const auto& view = mainCamera->GetViewMatrix();
        const auto& proj = mainCamera->GetProjectionMatrix();

        // --- MESH EDITOR OVERRIDE ---
        // If Mesh Editor is active and target matches selection, draw vertex gizmos
        auto* meshEditor = MeshEditorPanel::GetInstance();
        if ( meshEditor && meshEditor->GetTargetEntity() == selectedEntity && meshEditor->GetActiveMesh() )
        {
            auto& selection = meshEditor->GetSelection();
            if ( !selection.VertexIndices.empty() )
            {
                auto      mesh        = meshEditor->GetActiveMesh();
                glm::vec3 localCenter = selection.GetCenter( *mesh );
                glm::vec3 worldCenter = glm::vec3( modelMatrix * glm::vec4( localCenter, 1.0f ) );

                glm::mat4 gizmoTransform = glm::translate( glm::mat4( 1.0f ), worldCenter );

                if ( ImGuizmo::Manipulate( &view[0][0], &proj[0][0], ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                                           &gizmoTransform[0][0] ) )
                {
                    glm::vec3 newWorldCenter, scale, skew;
                    glm::quat rotation;
                    glm::vec4 perspective;
                    glm::decompose( gizmoTransform, scale, rotation, newWorldCenter, skew, perspective );

                    glm::vec3 deltaWorld = newWorldCenter - worldCenter;
                    glm::vec3 deltaLocal =
                         glm::vec3( glm::inverse( modelMatrix ) * glm::vec4( deltaWorld, 0.0f ) );

                    auto& vertices = mesh->GetVertices();
                    for ( auto idx : selection.VertexIndices )
                    {
                        vertices[idx].Position += deltaLocal;
                    }

                    mesh->Update( vertices, mesh->GetIndices() );
                }

                if ( ImGuizmo::IsOver() )
                    m_GizmoHovered = true;
                return; // Don't draw object gizmo if vertex gizmo is active
            }
        }

        // --- STANDARD OBJECT GIZMO ---
        if ( ImGuizmo::Manipulate( &view[0][0], &proj[0][0], static_cast<ImGuizmo::OPERATION>( m_GizmoType ),
                                   ImGuizmo::WORLD, &modelMatrix[0][0] ) )
        {
            if ( ImGuizmo::IsOver() )
            {
                m_GizmoHovered = true;
            }

            // Decompose and update transform
            glm::vec3 scale, translation, skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose( modelMatrix, scale, rotation, translation, skew, perspective );

            transformComponent.Translation = translation;
            transformComponent.Rotation    = glm::eulerAngles( rotation );
            transformComponent.Scale       = scale;
        }
    }

    std::pair<float, float> ViewportPanel::GetMouseViewportSpace() const
    {
        return { m_ViewportData.MousePosition.x, m_ViewportData.MousePosition.y };
    }

    void ViewportPanel::HandleObjectPicking()
    {
        const auto& mainCamera = m_Scene->GetMainCamera().lock();
        if ( !mainCamera )
        {
            return;
        }

        if ( m_GizmoHovered )
        {
            return;
        }

        // not over viewport
        if ( !m_ViewportData.IsHovered )
        {
            return;
        }
        auto [mouseX, mouseY] = GetMouseViewportSpace();
        const auto ray        = Common::Math::Ray::FromScreenPosition(
             { mouseX, mouseY }, mainCamera->GetProjectionMatrix(), mainCamera->GetViewMatrix(),
             mainCamera->GetPosition(), static_cast<uint32_t>( m_ViewportData.Size.x ),
             static_cast<uint32_t>( m_ViewportData.Size.y ) );

        float        closestT = std::numeric_limits<float>::max();
        Common::UUID selectedUUID;

        const auto entities = m_Scene->GetAllEntities();
        auto&      registry = m_Scene->GetRegistry();

        std::vector<std::pair<Common::UUID, std::pair<glm::mat4, Desert::Mesh*>>> allMeshes;

        for ( const auto& entity : entities )
        {
            if ( entity.HasComponent<ECS::StaticMeshComponent>() )
            {
                const auto mesh = GetMeshComponent( entity.GetComponent<ECS::StaticMeshComponent>() );
                if ( !mesh )
                {
                    continue;
                }

                allMeshes.push_back( { entity.GetComponent<ECS::UUIDComponent>().UUID,
                                       { entity.GetWorldTransform(), mesh } } );
            }
        }

        for ( const auto& [uuid, meshData] : allMeshes )
        {
            const auto& [transform, mesh] = meshData;
            float t                       = 0.0f;
            auto  localRay                = ray.ToLocalSpace( transform );

            for ( const auto& submesh : mesh->GetSubmeshes() )
            {
                if ( localRay.IntersectsAABB( submesh.BoundingBox, t ) )
                {
                    if ( t < closestT )
                    {
                        selectedUUID = uuid;
                        closestT     = t;
                    }
                }
            }
        }

        if ( closestT != std::numeric_limits<float>::max() )
        {
            // If the hit entity is a child of a prefab, select the prefab root so the
            // entire prefab gets outlined instead of just one submesh.
            auto hitEntityRef = m_Scene->FindEntityByID( selectedUUID );
            if ( hitEntityRef )
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

            Core::SelectionManager::SetSelected( selectedUUID );
        }
    }

    void ViewportPanel::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowResize>( [this]( Common::EventWindowResize& e )
                                                        { return OnWindowResize( e ); } );

        eventManager.Notify<Common::MouseButtonPressedEvent>( [this]( Common::MouseButtonPressedEvent& e )
                                                              { return OnMousePressed( e ); } );

        eventManager.Notify<Common::KeyPressedEvent>( [this]( Common::KeyPressedEvent& e )
                                                      { return OnKeyPressedEvent( e ); } );
    }

    bool ViewportPanel::OnWindowResize( Common::EventWindowResize& e )

    {
        // m_ImGuiLayer->Resize( e.width, e.height );
        // m_EditorCamera.UpdateProjectionMatrix( e.width, e.height );

        return false;
    }

    bool ViewportPanel::OnMousePressed( Common::MouseButtonPressedEvent& e )
    {
        if ( e.GetMouseButton() == Common::MouseButton::Left )
        {
            HandleObjectPicking();
        }

        return false;
    }

    bool ViewportPanel::OnKeyPressedEvent( Common::KeyPressedEvent& e )
    {
        switch ( e.GetKeyCode() )
        {
            case Common::KeyCode::Escape:
                m_GizmoType = GizmoType::None;
                break;
            case Common::KeyCode::T:
                m_GizmoType = GizmoType::Translate;
                break;
            case Common::KeyCode::R:
                m_GizmoType = GizmoType::Rotate;
                break;
            case Common::KeyCode::C:
                m_GizmoType = GizmoType::Scale;
                break;
        }
        return false;
    }

    Desert::Mesh* ViewportPanel::GetMeshComponent( const ECS::StaticMeshComponent& component )
    {
        if ( component.MeshHandle )
            return Runtime::ResourceRegistry::GetMeshService()->Get( component.MeshHandle );
        return component.RuntimeMesh ? component.RuntimeMesh.get() : nullptr;
    }

} // namespace Desert::Editor