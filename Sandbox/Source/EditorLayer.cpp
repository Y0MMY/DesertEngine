#define IMGUI_DEFINE_MATH_OPERATORS

#include "EditorLayer.hpp"

#include "Editor/Panels/SceneHierarchy/SceneHierarchyPanel.hpp"
#include "Editor/Panels/SceneProperties/ScenePropertiesPanel.hpp"
#include "Editor/Panels/Debug/ShaderLibraryPanel.hpp"
#include <ImGui/imgui_internal.h>

#include <ImGuizmo.h>
#include <ImGuizmo.cpp> //TODO: TEMP

#include <glm/gtx/matrix_decompose.hpp>

namespace Desert
{
    EditorLayer::EditorLayer( const std::shared_ptr<Common::Window>& window, const std::string& layerName )
         : Common::Layer( layerName ), m_Window( window )

    {
        m_AssetManager     = std::make_shared<Assets::AssetManager>();
        m_ResourceRegistry = std::make_shared<Runtime::ResourceRegistry>();
        m_AssetPreloader   = std::make_unique<Assets::AssetPreloader>( m_AssetManager, m_ResourceRegistry );
        m_MainScene        = std::make_shared<Core::Scene>( "New Scene", m_ResourceRegistry );
    }

    EditorLayer::~EditorLayer()
    {
        m_MainScene->Shutdown();
    }

    [[nodiscard]] Common::BoolResult EditorLayer::OnAttach()
    {
        // Setup ImGui docking
        ::ImGui::CreateContext();
        ImGuiIO& io = ::ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows

        // Setup ImGui style
        ::ImGui::StyleColorsDark();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to
        // regular ones
        ImGuiStyle& style = ::ImGui::GetStyle();
        if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
        {
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        m_MainScene->Init();
#ifdef EBABLE_IMGUI
        m_ImGuiLayer = ImGui::ImGuiLayer::Create();
        m_ImGuiLayer->OnAttach();
        m_UIHelper = std::make_unique<Editor::UI::UIHelper>();
        m_UIHelper->Init();

        m_AssetPreloader->PreloadAllAssets();

        m_Panels.emplace_back( std::make_unique<Editor::SceneHierarchyPanel>( m_MainScene, m_AssetManager ) );
        m_Panels.emplace_back(
             std::make_unique<Editor::ScenePropertiesPanel>( m_MainScene, m_AssetManager, m_ResourceRegistry ) );
        m_Panels.emplace_back( std::make_unique<Editor::ShaderLibraryPanel>() );

#endif // EBABLE_IMGUI

        return BOOLSUCCESS;
    }

    [[nodiscard]] Common::BoolResult EditorLayer::OnUpdate( const Common::Timestep& ts )
    {
        m_EditorCamera.OnUpdate( ts );
        const auto& beginResult = m_MainScene->BeginScene( m_EditorCamera );
        if ( !beginResult )
        {
            return Common::MakeError( beginResult.GetError() );
        }

        m_MainScene->OnUpdate( ts );

        const auto& endResult = m_MainScene->EndScene();
        m_Window->PrepareNextFrame();

        if ( !endResult )
        {
            return Common::MakeError( endResult.GetError() );
        }

        {
            m_ImGuiLayer->Begin();

            OnImGuiRender();

            m_ImGuiLayer->End();
        }
        m_Window->PresentFinalImage();
        return BOOLSUCCESS;
    }

    Common::BoolResult EditorLayer::OnImGuiRender()
    {
        ImGuizmo::BeginFrame();
        static bool               dockspaceOpen   = true;
        static bool               opt_fullscreen  = true;
        static bool               opt_padding     = false;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
        // because it would be confusing to have two docking targets within each others.
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if ( opt_fullscreen )
        {
            const ImGuiViewport* viewport = ::ImGui::GetMainViewport();
            ::ImGui::SetNextWindowPos( viewport->Pos );
            ::ImGui::SetNextWindowSize( viewport->Size );
            ::ImGui::SetNextWindowViewport( viewport->ID );
            ::ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
            ::ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }
        else
        {
            dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
        }

        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
        // and handle the pass-thru hole, so we ask Begin() to not render a background.
        if ( dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode )
            window_flags |= ImGuiWindowFlags_NoBackground;

        // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
        // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
        // all active windows docked into it will lose their parent and become undocked.
        // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
        // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
        if ( !opt_padding )
            ::ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
        ::ImGui::Begin( "DockSpace Demo", &dockspaceOpen, window_flags );
        if ( !opt_padding )
            ::ImGui::PopStyleVar();

        if ( opt_fullscreen )
            ::ImGui::PopStyleVar( 2 );

        // Submit the DockSpace
        ImGuiIO& io = ::ImGui::GetIO();
        if ( io.ConfigFlags & ImGuiConfigFlags_DockingEnable )
        {
            ImGuiID dockspace_id = ::ImGui::GetID( "MyDockSpace" );
            ::ImGui::DockSpace( dockspace_id, ImVec2( 0.0f, 0.0f ), dockspace_flags );
        }

        // Menu Bar
        if ( ::ImGui::BeginMenuBar() )
        {
            if ( ::ImGui::BeginMenu( "File" ) )
            {
                if ( ::ImGui::MenuItem( "Exit" ) )
                {
                }

                if ( ::ImGui::MenuItem( "Save scene" ) )
                {
                    m_MainScene->Serialize();
                }

                if ( ::ImGui::MenuItem( "Show shaders info" ) )
                {
                    static_cast<Editor::ShaderLibraryPanel*>( m_Panels[2].get() )
                         ->ToggleVisibility(); // HACK (bad way)
                }
                ::ImGui::EndMenu();
            }

            ::ImGui::EndMenuBar();
        }

        // Apply company styling to the viewport
        ::ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
        ::ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 4.0f );
        ::ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
        ::ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.15f, 0.15f, 0.17f, 1.0f ) );

        ::ImGui::Begin( "Viewport", nullptr, ImGuiWindowFlags_NoScrollbar );
        {
            ImGuiWindow* window      = ::ImGui::GetCurrentWindow();
            ImVec2       mousePos    = ::ImGui::GetMousePos();
            ImVec2       viewportPos = ::ImGui::GetWindowPos();

            m_ViewportData.MousePosition = glm::vec2( mousePos.x - viewportPos.x, mousePos.y - viewportPos.y );

            m_ViewportData.Size      = { ::ImGui::GetContentRegionAvail().x, ::ImGui::GetContentRegionAvail().y };
            m_ViewportData.IsHovered = ::ImGui::IsWindowHovered();

            m_UIHelper->Image( m_MainScene->GetFinalImage(), { m_ViewportData.Size.x, m_ViewportData.Size.y } );
        }

        m_GizmoHovered = false;

        // Gizmos
        if ( m_GizmoType != GizmoType::None )
        {
            const auto& selected = Editor::Core::SelectionManager::GetSelected();
            if ( selected )
            {
                const auto& selectedEntity = m_MainScene->FindEntityByID( *selected );
                if ( selectedEntity )
                {
                    auto& transformComponent = selectedEntity->get().GetComponent<ECS::TransformComponent>();
                    auto  transform          = transformComponent.GetTransform();

                    float rw = (float)::ImGui::GetWindowWidth();
                    float rh = (float)::ImGui::GetWindowHeight();
                    ImGuizmo::SetOrthographic( false );
                    ImGuizmo::SetDrawlist();
                    ImGuizmo::SetRect( ::ImGui::GetWindowPos().x, ::ImGui::GetWindowPos().y, rw, rh );
                    ImGuizmo::Manipulate( &m_EditorCamera.GetViewMatrix()[0][0],
                                          &m_EditorCamera.GetProjectionMatrix()[0][0],
                                          (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::WORLD, &transform[0][0] );

                    if ( ImGuizmo::IsOver() )
                    {
                        m_GizmoHovered = true;
                    }

                    // TODO: use math class
                    glm::vec3 scale;
                    glm::quat rotation;
                    glm::vec3 translation;
                    glm::vec3 skew;
                    glm::vec4 perspective;
                    glm::decompose( transform, scale, rotation, translation, skew, perspective );

                    glm::vec3 euler = glm::eulerAngles( rotation );

                    transformComponent.Translation = translation;
                    transformComponent.Rotation    = euler;
                    transformComponent.Scale       = scale;
                }
            }
        }

        ::ImGui::End();
        ::ImGui::PopStyleColor( 1 );
        ::ImGui::PopStyleVar( 3 );

        for ( const auto& panel : m_Panels )
        {
            if ( !panel->GetVisibility() )
            {
                continue;
            }

            namespace ImGui = ::ImGui;
            ImGuiWindowClass window_class;
            window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
            ImGui::SetNextWindowClass( &window_class );

            ImGui::Begin( panel->GetName().c_str() );
            panel->OnUIRender();
            ImGui::End();
        }

        ::ImGui::End(); // End dockspace

        return BOOLSUCCESS;
    }

    void EditorLayer::HandleObjectPicking()
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

        const auto entities = m_MainScene->GetAllEntities();

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
                        Editor::Core::SelectionManager::SetSelected( uuid );
                        closestT = t;

                        return;
                    }
                }
            }
        }
    }

    void EditorLayer::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowResize>( [this]( Common::EventWindowResize& e )
                                                        { return OnWindowResize( e ); } );

        eventManager.Notify<Common::MouseButtonPressedEvent>( [this]( Common::MouseButtonPressedEvent& e )
                                                              { return OnMousePressed( e ); } );

        eventManager.Notify<Common::KeyPressedEvent>( [this]( Common::KeyPressedEvent& e )
                                                      { return OnKeyPressedEvent( e ); } );
    }

    bool EditorLayer::OnWindowResize( Common::EventWindowResize& e )

    {
        // m_ImGuiLayer->Resize( e.width, e.height );
        m_EditorCamera.UpdateProjectionMatrix( e.width, e.height );
        m_MainScene->Resize( e.width, e.height );
        return false;
    }

    std::pair<float, float> EditorLayer::GetMouseViewportSpace()
    {
        return { m_ViewportData.MousePosition.x, m_ViewportData.MousePosition.y };
    }

    bool EditorLayer::OnMousePressed( Common::MouseButtonPressedEvent& e )
    {
        if ( e.GetMouseButton() == Common::MouseButton::Left )
        {
            HandleObjectPicking();
        }

        return false;
    }

    bool EditorLayer::OnKeyPressedEvent( Common::KeyPressedEvent& e )
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

    Common::BoolResult EditorLayer::OnDetach()
    {
#ifdef EBABLE_IMGUI
        m_ImGuiLayer->OnDetach();
        m_ImGuiLayer.reset();

#endif // EBABLE_IMGUI
        return BOOLSUCCESS;
    }

} // namespace Desert