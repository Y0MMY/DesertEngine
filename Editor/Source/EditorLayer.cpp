#define IMGUI_DEFINE_MATH_OPERATORS

#include "EditorLayer.hpp"

#include "Editor/Panels/SceneHierarchy/SceneHierarchyPanel.hpp"
#include "Editor/Panels/SceneProperties/ScenePropertiesPanel.hpp"
#include "Editor/Core/EditorResources.hpp"
#include "Editor/Core/ThemeManager.hpp"
#include "Editor/Panels/Debug/ShaderLibraryPanel.hpp"
#include "Editor/Panels/FileExplorer/FileExplorerPanel.hpp"
#include "Editor/Panels/ViewportPanel/ViewportPanel.hpp"
#include "Editor/Core/EditorResources.hpp"
#include "Editor/Core/ImGuiUtilities.hpp"
#include <ImGui/imgui_internal.h>

#include <glm/gtx/matrix_decompose.hpp>

namespace Desert::Editor
{
    static constexpr uint32_t s_ShaderLibraryPanelIndex = 2;

    EditorLayer::EditorLayer( const Engine::Application* application, const std::string& layerName )
         : Common::Layer( layerName ), m_Application( application )

    {
        m_AssetManager   = std::make_shared<Assets::AssetManager>();
        m_AssetPreloader = std::make_unique<Assets::AssetPreloader>( m_AssetManager );
        m_MainScene      = std::make_shared<Desert::Core::Scene>( "New Scene" );

        // PrimitiveMeshFactory::Initialize();
    }

    EditorLayer::~EditorLayer()
    {
        // PrimitiveMeshFactory::Shutdown();
        m_MainScene->Shutdown();
    }

    [[nodiscard]] Common::BoolResult EditorLayer::OnAttach()
    {
        // Setup ImGui docking
        ::ImGui::CreateContext();

        Editor::EditorResources::Initialize( "Resources/Fonts/materialdesignicons-webfont.ttf" );

        ImGuiIO& io = ::ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows

        // Setup ImGui style
        ThemeManager::SetBlackTheme();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to
        // regular ones
        ImGuiStyle& style = ::ImGui::GetStyle();
        if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
        {
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        Editor::ThemeManager::SetDarkTheme();

        m_AssetPreloader->PreloadAllAssets();
        m_MainScene->Init();
#ifdef EBABLE_IMGUI
        m_ImGuiLayer = ImGui::ImGuiLayer::Create();
        m_ImGuiLayer->OnAttach();


        m_Panels.emplace_back( std::make_unique<Editor::SceneHierarchyPanel>( m_MainScene ) );
        m_Panels.emplace_back(
             std::make_unique<Editor::ScenePropertiesPanel>( m_MainScene, m_AssetManager ) );
        m_Panels.emplace_back( std::make_unique<Editor::ShaderLibraryPanel>() );
        m_Panels.emplace_back( std::make_unique<Editor::ViewportPanel>( m_MainScene ) );
        m_Panels.emplace_back( std::make_unique<Editor::FileExplorerPanel>( "Resources/" ) );

#endif // EBABLE_IMGUI

        m_RenderRegistry = std::make_unique<Render::RenderRegistry>( m_MainScene );
        return BOOLSUCCESS;
    }

    [[nodiscard]] Common::BoolResult EditorLayer::OnUpdate( const Common::Timestep& ts )
    {
        const auto& beginResult = m_MainScene->BeginScene();
        if ( !beginResult )
        {
            return Common::MakeError( beginResult.GetError() );
        }
        m_RenderRegistry->Render();

        m_MainScene->OnUpdate( ts );

        const auto& endResult = m_MainScene->EndScene();
        m_Application->GetWindow()->PrepareNextFrame();

        if ( !endResult )
        {
            return Common::MakeError( endResult.GetError() );
        }

        {
            m_ImGuiLayer->Begin();

            OnImGuiRender();

            m_ImGuiLayer->End();
        }

        m_Application->GetWindow()->PresentFinalImage();
        return BOOLSUCCESS;
    }

    Common::BoolResult EditorLayer::OnImGuiRender()
    {

        static bool               dockspaceOpen  = true;
        static bool               opt_fullscreen = true;
        static ImGuiDockNodeFlags dockspace_flags =
             ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton;

        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
        // because it would be confusing to have two docking targets within each others.
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

        // Menu Bar
        ::ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
        DrawMenuBar();
        ::ImGui::PopStyleVar();

        if ( opt_fullscreen )
        {
            const ImGuiViewport* viewport = ::ImGui::GetMainViewport();

            auto pos     = viewport->Pos;
            auto size    = viewport->Size;
            bool menuBar = true;
            if ( menuBar )
            {
                const float infoBarSize = ::ImGui::GetFrameHeight();
                pos.y += infoBarSize;
                size.y -= infoBarSize;
            }

            ::ImGui::SetNextWindowPos( pos );
            ::ImGui::SetNextWindowSize( size );
            ::ImGui::SetNextWindowViewport( viewport->ID );

            ::ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
            ::ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
        // and handle the pass-thru hole, so we ask Begin() to not render a background.
        if ( dockspace_flags & ImGuiDockNodeFlags_DockSpace )
            window_flags |= ImGuiWindowFlags_NoBackground;

        // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
        // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
        // all active windows docked into it will lose their parent and become undocked.
        // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
        // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
        ::ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
        ::ImGui::Begin( "DockSpace Demo", &dockspaceOpen, window_flags );
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

        for ( const auto& panel : m_Panels )
        {
            if ( !panel->GetVisibility() )
            {
                continue;
            }

            namespace ImGui = ::ImGui;
            if ( panel->GetName() == "Scene###scene" ) // TODO: Panel props
            {
                ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
            }
            ImGui::Begin( panel->GetName().c_str() );
            panel->OnUIRender();
            if ( panel->GetName() == "Scene###scene" )
            {
                ImGui::PopStyleVar();
            }
            ImGui::End();
        }

        ::ImGui::End(); // End dockspace

        return BOOLSUCCESS;
    }

    void EditorLayer::DrawMenuBar()
    {
        namespace ImGui = ::ImGui;

        bool openSaveScenePopup   = false;
        bool openNewScenePopup    = false;
        bool openReloadScenePopup = false;
        bool openProjectLoadPopup = true; // TOOD

        if ( ImGui::BeginMainMenuBar() )
        {
            if ( ImGui::BeginMenu( "File" ) )
            {
                if ( ImGui::MenuItem( "Open Project" ) )
                {
                }

                ImGui::Separator();

                if ( ImGui::MenuItem( "Open File" ) )
                {
                }

                ImGui::Separator();

                if ( ImGui::MenuItem( "New Scene", "CTRL+N" ) )
                {
                }

                if ( ImGui::MenuItem( "Save Scene", "CTRL+S" ) )
                {
                }

                if ( ImGui::MenuItem( "Reload Scene", "CTRL+R" ) )
                {
                }

                ImGui::Separator();

                if ( ImGui::BeginMenu( "Style" ) )
                {
                    if ( ImGui::MenuItem( "Dark", "" ) )
                    {
                        ThemeManager::SetDarkTheme();
                    }
                    if ( ImGui::MenuItem( "Black", "" ) )
                    {
                        ThemeManager::SetBlackTheme();
                    }

                    ImGui::EndMenu();
                }

                ImGui::Separator();

                if ( ImGui::MenuItem( "Exit" ) )
                {
                }

                ImGui::EndMenu();
            }
            if ( ImGui::BeginMenu( "Edit" ) )
            {

                ImGui::EndMenu();
            }
            if ( ImGui::BeginMenu( "View" ) )
            {
                for ( auto& panel : m_Panels )
                {
                    if ( ImGui::MenuItem( panel->GetName().c_str(), "", &panel->GetVisibility(), true ) )
                    {
                        // panel->SetActive( true );
                    }
                }

                ImGui::EndMenu();
            }

            if ( ImGui::BeginMenu( "Scenes" ) )
            {
                ImGui::EndMenu();
            }

            if ( ImGui::BeginMenu( "Graphics" ) )
            {
                ImGui::EndMenu();
            }

            if ( ImGui::BeginMenu( "About" ) )
            {
                ImGui::EndMenu(); // About Menu
            }

            ImGui::PushFont( Editor::EditorResources::GetBoldFont() );
            ImGui::PushStyleColor( ImGuiCol_Border, IM_COL32( 40, 40, 40, 255 ) );

            ImGui::SameLine( ImGui::GetCursorPosX() + 40.0f );
            ImGui::Separator();
            ImGui::SameLine();
            ImGui::TextUnformatted( "Project name" );

            Utils::ImGuiUtilities::Tooltip( "project settings" );
            Utils::ImGuiUtilities::DrawBorder(
                 Utils::ImGuiUtilities::RectExpanded( Utils::ImGuiUtilities::GetItemRect(), 24.0f, 68.0f ), 1.0f,
                 3.0f, 0.0f, -60.0f );

            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine( ImGui::GetCursorPosX() + 32.0f );
            ImGui::TextUnformatted( m_MainScene->GetSceneName().c_str() );

            ImGui::PopFont();
            ImGui::PopStyleColor();

            ImGui::SameLine( ( ImGui::GetWindowContentRegionMax().x * 0.5f ) -
                             ( 1.5f * ( ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.x ) ) );

            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.1f, 0.2f, 0.7f, 0.0f ) );

            bool selected;
            {
                selected = m_EditorState == EditorState::Play;
                if ( selected )
                {
                    ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetSelectedColor() );
                }

                if ( ImGui::Button( ICON_MDI_PLAY ) )
                {

                    ImGui::SetWindowFocus( ICON_MDI_GAMEPAD_VARIANT " Game###game" );

                    // m_SelectedEntities.clear();
                    // m_SelectedEntity = entt::null;
                    if ( selected )
                    {
                        ImGui::SetWindowFocus( "###scene" );
                    }
                    else
                    {
                        ImGui::SetWindowFocus( "###game" );
                    }
                }
                if ( ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip( "Play" );
                }

                if ( selected )
                {
                    ImGui::PopStyleColor();
                }
            }

            ImGui::SameLine();

            {
                selected = m_EditorState == EditorState::Paused;
                if ( selected )
                {
                    ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetSelectedColor() );
                }

                if ( ImGui::Button( ICON_MDI_PAUSE ) )
                {
                }

                if ( ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip( "Pause" );
                }

                if ( selected )
                {
                    ImGui::PopStyleColor();
                }
            }

            ImGui::SameLine();

            const auto text = m_Application->GetEngineStats().GetFormattedStats();
            auto       size = ImGui::CalcTextSize( text.c_str() );
            ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - size.x -
                             ImGui::GetStyle().ItemSpacing.x * 2.0f );
            ImGui::Text( text.c_str() );
            ImGui::PopStyleColor();

            ImGui::EndMainMenuBar();
        }

        if ( openSaveScenePopup )
            ImGui::OpenPopup( "Save Scene" );

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos( center, ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );

        if ( ImGui::BeginPopupModal( "Save Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::Text( "Save Current Scene Changes?\n\n" );
            ImGui::Separator();

            if ( ImGui::Button( "OK", ImVec2( 120, 0 ) ) )
            {

                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if ( openNewScenePopup )
        {
            ImGui::OpenPopup( "New Scene" );
        }

        if ( ImGui::BeginPopupModal( "New Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            if ( ImGui::Button( "Save Current Scene Changes" ) )
            {
            }

            ImGui::Text( "Create New Scene?\n\n" );
            ImGui::Separator();

            static bool defaultSetup = false;

            static std::string newSceneName = "NewScene";
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( "Name : " );
            ImGui::SameLine();
            Utils::ImGuiUtilities::InputText( newSceneName, "##SceneNameChange" );

            ImGui::Checkbox( "Default Setup", &defaultSetup );

            ImGui::Separator();

            if ( ImGui::Button( "OK", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if ( ImGui::BeginPopupModal( "Open Project", NULL, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            if ( ImGui::Button( "Load Project" ) )
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::Separator();
            ImGui::TextUnformatted( "Create New Project?\n" );

            static std::string newProjectName = "New Project Name";
            Utils::ImGuiUtilities::InputText( newProjectName, "##ProjectNameChange" );

            if ( ImGui::Button( ICON_MDI_FOLDER ) )
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            ImGui::TextUnformatted( "prjoect lcoation" );

            ImGui::Separator();

            if ( ImGui::Button( "Create", ImVec2( 120, 0 ) ) )
            {

                ImGui::CloseCurrentPopup();
            }

            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if ( ImGui::Button( "Exit", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if ( openReloadScenePopup )
        {
            ImGui::OpenPopup( "Reload Scene" );
        }

        if ( ImGui::BeginPopupModal( "Reload Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::Text( "Reload Scene?\n\n" );
            ImGui::Separator();

            if ( ImGui::Button( "OK", ImVec2( 120, 0 ) ) )
            {

                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    Common::BoolResult EditorLayer::OnDetach()
    {
#ifdef EBABLE_IMGUI
        m_ImGuiLayer->OnDetach();
        m_ImGuiLayer.reset();

#endif // EBABLE_IMGUI
        return BOOLSUCCESS;
    }

} // namespace Desert::Editor