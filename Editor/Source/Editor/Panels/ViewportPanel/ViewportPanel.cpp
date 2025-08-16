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
    }

    void ViewportPanel::OnUIRender()
    {
        ImVec2 mousePos    = ImGui::GetMousePos();
        ImVec2 viewportPos = ImGui::GetWindowPos();

        m_ViewportData.MousePosition = glm::vec2( mousePos.x - viewportPos.x, mousePos.y - viewportPos.y );

        const glm::vec2 size = { ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        if ( m_ViewportData.Size != size )
        {
            m_ViewportData.Size = size;
            m_EditorCamera.UpdateProjectionMatrix( size.x, size.y );
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
    }

    void ViewportPanel::RenderGizmo()
    {
        ImGuizmo::BeginFrame();

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

        ImGuizmo::Manipulate( &m_EditorCamera.GetViewMatrix()[0][0], &m_EditorCamera.GetProjectionMatrix()[0][0],
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
             { mouseX, mouseY }, m_EditorCamera.GetProjectionMatrix(), m_EditorCamera.GetViewMatrix(),
             m_EditorCamera.GetPosition(), static_cast<uint32_t>( m_ViewportData.Size.x ),
             static_cast<uint32_t>( m_ViewportData.Size.y ) );

        float closestT = std::numeric_limits<float>::max();

        const auto entities = m_Scene->GetAllEntities();

        std::vector<std::pair<Common::UUID, std::pair<glm::mat4, std::shared_ptr<Desert::Mesh>>>> allMeshes;

        for ( const auto& entity : entities )
        {
            if ( entity.HasComponent<ECS::StaticMeshComponent>() )
            {
                const auto& mesh =
                     m_ResourceRegistry->GetMesh( entity.GetComponent<ECS::StaticMeshComponent>().MeshHandle );
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

    void ViewportPanel::UpdateCamera( const Common::Timestep& timestep )
    {
        m_EditorCamera.OnUpdate( timestep );
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
        m_EditorCamera.UpdateProjectionMatrix( e.width, e.height );
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

} // namespace Desert::Editor