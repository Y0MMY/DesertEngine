#include "ViewportPanel.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>

#include <ImGuizmo.h>
#include <glm/gtx/matrix_decompose.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;
    ViewportPanel::ViewportPanel( const std::shared_ptr<Desert::Core::Scene>&       scene,
                                  const std::shared_ptr<Runtime::ResourceRegistry>& resourceRegistry )
         : IPanel( "Scene###scene" ), m_Scene( scene ), m_ResourceRegistry( resourceRegistry )
    {
        m_UIHelper = std::make_unique<Editor::UI::UIHelper>();
        m_UIHelper->Init();

        m_LightGizmoRenderer = std::make_unique<LightGizmoRenderer>( scene );
    }

    void ViewportPanel::OnUIRender()
    {
        const auto& mainCamera = m_Scene->GetMainCamera();
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

        m_ViewportData.MousePosition = glm::vec2( mousePos.x - ( viewportPos.x + viewportMin.x ),
                                                  mousePos.y - ( viewportPos.y + viewportMin.y ) );

        m_ViewportData.Size      = { viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y };
        m_ViewportData.IsHovered = ::ImGui::IsWindowHovered();

        mainCamera.value()->UpdateProjectionMatrix( m_ViewportData.Size.x,
                                                    m_ViewportData.Size.y ); // TODO: Move to scene
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

    void ViewportPanel::RenderGizmo()
    {
        ImGuizmo::BeginFrame();

        const auto& mainCamera = m_Scene->GetMainCamera();
        if ( !mainCamera )
        {
            return;
        }

        const auto& selected = Core::SelectionManager::GetSelected();
        if ( !selected )
            return;

        const auto& selectedEntity = m_Scene->FindEntityByID( *selected );
        if ( !selectedEntity )
            return;

        auto& transformComponent = selectedEntity->get().GetComponent<ECS::TransformComponent>();
        auto  transform          = transformComponent.GetTransform();

        float rw = static_cast<float>( ImGui::GetWindowWidth() );
        float rh = static_cast<float>( ImGui::GetWindowHeight() );

        ImGuizmo::SetOrthographic( false );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, rw, rh );

        ImGuizmo::Manipulate( &mainCamera.value()->GetViewMatrix()[0][0],
                              &mainCamera.value()->GetProjectionMatrix()[0][0],
                              static_cast<ImGuizmo::OPERATION>( m_GizmoType ), ImGuizmo::WORLD, &transform[0][0] );

        if ( ImGuizmo::IsOver() )
        {
            m_GizmoHovered = true;
        }

        // Decompose and update transform
        glm::vec3 scale, translation, skew;
        glm::quat rotation;
        glm::vec4 perspective;
        glm::decompose( transform, scale, rotation, translation, skew, perspective );

        transformComponent.Translation = translation;
        transformComponent.Rotation    = glm::eulerAngles( rotation );
        transformComponent.Scale       = scale;
    }

    std::pair<float, float> ViewportPanel::GetMouseViewportSpace() const
    {
        return { m_ViewportData.MousePosition.x, m_ViewportData.MousePosition.y };
    }

    void ViewportPanel::HandleObjectPicking()
    {
        const auto& mainCamera = m_Scene->GetMainCamera();
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
             { mouseX, mouseY }, mainCamera.value()->GetProjectionMatrix(), mainCamera.value()->GetViewMatrix(),
             mainCamera.value()->GetPosition(), static_cast<uint32_t>( m_ViewportData.Size.x ),
             static_cast<uint32_t>( m_ViewportData.Size.y ) );

        float closestT = std::numeric_limits<float>::max();

        const auto entities = m_Scene->GetAllEntities();

        std::vector<std::pair<Common::UUID, std::pair<glm::mat4, std::shared_ptr<Desert::Mesh>>>> allMeshes;

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
                                       { entity.GetComponent<ECS::TransformComponent>().GetTransform(), mesh } } );
            }
        }

        for ( const auto& [uuid, meshData] : allMeshes )
        {
            const auto& [transform, mesh] = meshData;
            float t                       = 0.0f;

            for ( const auto& submesh : mesh->GetSubmeshes() )
            {
                auto localRay = ray.ToLocalSpace( transform );

                if ( localRay.IntersectsAABB( submesh.BoundingBox, t ) )
                {
                    LOG_TRACE( "Picked object: {} distance: {}", submesh.Name, t );
                    if ( t < closestT )
                    {
                        Core::SelectionManager::SetSelected( uuid );
                        closestT = t;

                        return;
                    }
                }
            }
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
        m_Scene->Resize( e.width, e.height );
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

    std::shared_ptr<Desert::Mesh> ViewportPanel::GetMeshComponent( const ECS::StaticMeshComponent& component )
    {
        if ( component.GetMeshType() == ECS::StaticMeshComponent::Type::Asset )
        {
            return m_ResourceRegistry->GetMesh( *component.MeshHandle );
        }

        return PrimitiveMeshFactory::GetPrimitive( *component.PrimitiveShape );
    }

} // namespace Desert::Editor