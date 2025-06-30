#include "EditorLayer.hpp"

#include "Editor/Panels/SceneHierarchy/SceneHierarchyPanel.hpp"
#include "Editor/Panels/SceneProperties/ScenePropertiesPanel.hpp"
#include <ImGui/imgui_internal.h>

namespace Desert
{
    EditorLayer::EditorLayer( const std::shared_ptr<Common::Window>& window, const std::string& layerName )
         : Common::Layer( layerName ), m_Window( window )
    {
        m_AssetManager           = std::make_shared<Assets::AssetManager>();
        m_RuntimeResourceManager = std::make_shared<Runtime::RuntimeResourceManager>( m_AssetManager );
        m_MainScene              = std::make_shared<Core::Scene>( "New Scene", m_RuntimeResourceManager );
    }

    EditorLayer::~EditorLayer()
    {
        m_RuntimeResourceManager->Shutdown();
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

        m_Panels.emplace_back( std::make_unique<Editor::SceneHierarchyPanel>( m_MainScene, m_AssetManager ) );
        m_Panels.emplace_back(
             std::make_unique<Editor::ScenePropertiesPanel>( m_MainScene, m_RuntimeResourceManager ) );

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
            ImVec2 viewportSize = ::ImGui::GetContentRegionAvail();
            if ( m_Size.x != viewportSize.x || m_Size.y != viewportSize.y )
            {
                m_EditorCamera.UpdateProjectionMatrix( viewportSize.x, viewportSize.y );
                m_Size = viewportSize;
            }

            m_UIHelper->Image( m_MainScene->GetFinalImage(), viewportSize );
        }
        ::ImGui::End();
        ::ImGui::PopStyleColor( 1 );
        ::ImGui::PopStyleVar( 3 );

        for ( const auto& panel : m_Panels )
        {
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

    void EditorLayer::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowResize>( [this]( Common::EventWindowResize& e )
                                                        { return OnWindowResize( e ); } );
    }

    bool EditorLayer::OnWindowResize( Common::EventWindowResize& e )

    {
        // m_ImGuiLayer->Resize( e.width, e.height );
        m_MainScene->Resize( e.width, e.height );
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