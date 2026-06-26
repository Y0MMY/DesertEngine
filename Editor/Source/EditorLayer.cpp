#define IMGUI_DEFINE_MATH_OPERATORS

#include "EditorLayer.hpp"
#include <Common/Core/Core.hpp>

// 1. Engine Core
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Core/Serialize/SceneSerializer.hpp>

// 2. Editor Base & Infrastructure
#include "Editor/Core/EditorResources.hpp"
#include "Editor/Core/ThemeManager.hpp"
#include "Editor/Core/ImGuiUtilities.hpp"
#include <ImGui/imgui_internal.h>
#include <ImGuizmo.h>
#include "Editor/Import/ImportManager.hpp"
#include "Editor/Builtin/BuiltinMeshRegistry.hpp"

// 3. Editor Panels
#include "Editor/Panels/SceneHierarchy/SceneHierarchyPanel.hpp"
#include "Editor/Panels/SceneProperties/ScenePropertiesPanel.hpp"
#include "Editor/Panels/Debug/ShaderLibraryPanel.hpp"
#include "Editor/Panels/FileExplorer/FileExplorerPanel.hpp"
#include "Editor/Panels/ViewportPanel/ViewportPanel.hpp"
#include "Editor/Panels/MeshEditor/MeshEditorPanel.hpp"
#include "Editor/Panels/SceneSettings/SceneSettingsPanel.hpp"
#include "Editor/Panels/Logs/LogsPanel.hpp"

// 4. Misc
#include <glm/gtx/matrix_decompose.hpp>
#include <Engine/ECS/System/MeshECSSystem.hpp>
#include <Engine/ECS/System/SkyboxECSSystem.hpp>
#include <Engine/ECS/System/PointLightSystem.hpp>
#include <Engine/ECS/System/SpotLightSystem.hpp>
#include <Engine/ECS/System/AnimationECSSystem.hpp>

namespace Desert::Editor
{
    static constexpr uint32_t s_ShaderLibraryPanelIndex = 2;

    EditorLayer::EditorLayer( const Engine::Application* application, const std::string& layerName )
         : Common::Layer( layerName ), m_Application( application )

    {
        m_AssetManager = std::make_shared<Assets::AssetManager>();

        m_ImportManager = std::make_unique<ImportManager>();
        // Cook only what's missing/stale (skips the expensive Assimp re-parse on every launch).
        m_ImportManager->ImportAllFromDirectory( "Resources/Mesh/" ); // TEMP path

        m_AssetPreloader   = std::make_unique<Assets::AssetPreloader>( m_AssetManager );
        m_AnimationLibrary = std::make_unique<Animation::AnimationLibrary>( m_AssetManager.get() );
        m_SceneRenderer    = std::make_unique<Graphic::SceneRenderer>();
        m_MainScene        = std::make_shared<Desert::Core::Scene>( "New Scene", m_SceneRenderer.get() );

        BuiltinMeshRegistry::Init( nullptr );

        //LoadScene( "Resources/Assets/Scene/HouseDemo.desce" );
        }

    EditorLayer::~EditorLayer() = default;

    [[nodiscard]] Common::BoolResultStr EditorLayer::OnAttach()
    {
        // 1. Create ImGui Context first
        ::ImGui::CreateContext();

        // 2. Initialize Editor Resources (Adds fonts to the atlas)
        Editor::EditorResources::Initialize( "Resources/Fonts/materialdesignicons-webfont.ttf" );

#ifdef EBABLE_IMGUI
        // 3. Initialize Engine ImGui Layer (Initializes backend and uploads fonts)
        m_ImGuiLayer = ImGui::ImGuiLayer::Create();
        m_ImGuiLayer->OnAttach();
#endif // EBABLE_IMGUI

        ImGuiIO& io = ::ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows

        // Setup ImGui style
        ThemeManager::SetDarkTheme();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to
        // regular ones
        ImGuiStyle& style = ::ImGui::GetStyle();
        if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
        {
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        m_AssetPreloader->PreloadAllAssets();

        m_MainScene->AddSystem<ECS::MeshECSSystem>();
        m_MainScene->AddSystem<ECS::SkyboxECSSystem>();
        m_MainScene->AddSystem<ECS::PointLightECSSystem>();
        m_MainScene->AddSystem<ECS::SpotLightECSSystem>();
        m_MainScene->AddSystem<ECS::AnimationECSSystem>( m_AnimationLibrary.get() );

        const auto animations = m_AssetManager->FindAllByType<Assets::AnimationAsset>();

        for ( const auto& [handle, anim] : animations )
        {
            if ( !anim )
                continue;

            m_AnimationLibrary->Register( anim );
        }

        m_MainScene->Init();
#ifdef EBABLE_IMGUI
        m_Panels.emplace_back( std::make_unique<Editor::SceneHierarchyPanel>( m_MainScene, m_AssetManager ) );
        m_Panels.emplace_back( std::make_unique<Editor::ScenePropertiesPanel>( m_MainScene, m_AssetManager,
                                                                               m_AnimationLibrary.get() ) );
        m_Panels.emplace_back( std::make_unique<Editor::ShaderLibraryPanel>() );
        m_Panels.emplace_back( std::make_unique<Editor::ViewportPanel>( m_MainScene ) );
        {
            auto fileExplorer   = std::make_unique<Editor::FileExplorerPanel>( "Resources/", m_AssetManager.get() );
            m_FileExplorerPanel = fileExplorer.get();
            m_Panels.emplace_back( std::move( fileExplorer ) );
        }
        m_Panels.emplace_back( std::make_unique<Editor::MeshEditorPanel>( m_MainScene ) );
        m_Panels.emplace_back( std::make_unique<Editor::SceneSettingsPanel>( m_MainScene ) );
        m_Panels.emplace_back( std::make_unique<Editor::LogsPanel>() );
#endif // EBABLE_IMGUI

        m_RenderRegistry = std::make_unique<Render::RenderRegistry>( m_MainScene );
        return BOOLSUCCESS;
    }

    [[nodiscard]] Common::BoolResultStr EditorLayer::OnUpdate( const Common::Timestep& ts )
    {
        if ( m_SceneLoadRequested )
        {
            auto path = m_SceneLoadRequested.value();
            m_SceneLoadRequested.reset();
            LoadSceneInternal( path );
        }

        // Apply any deferred panel state (e.g. viewport resize) before scene rendering.
        // Panels defer GPU-side resize from OnUIRender to here so descriptor set pools are
        // never destroyed while their DS are bound to the recording command buffer.
        for ( auto& panel : m_Panels )
            panel->OnPreUpdate();

        const auto& beginResult = m_MainScene->BeginScene();
        if ( !beginResult )
        {
            return Common::MakeError( beginResult.GetError() );
        }
        m_RenderRegistry->Render();

        m_MainScene->OnUpdate( ts );

        const auto& endResult = m_MainScene->EndScene();

        if ( !endResult )
        {
            return Common::MakeError( endResult.GetError() );
        }

        return BOOLSUCCESS;
    }

    Common::BoolResultStr EditorLayer::OnImGuiRender()
    {
#ifdef EBABLE_IMGUI
        m_ImGuiLayer->Begin();
#endif

        // ImGuizmo is a single global per-frame state — begin it ONCE here, before any panel issues a
        // Manipulate(). Both the viewport's object gizmo and the Mesh Editor's vertex gizmo rely on this.
        ImGuizmo::BeginFrame();

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

#ifdef EBABLE_IMGUI
        m_ImGuiLayer->End();
#endif
        return BOOLSUCCESS;
    }

    void EditorLayer::DrawMenuBar()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMainMenuBar() )
            return;

        DrawFileMenu();
        DrawEditMenu();
        DrawViewMenu();
        DrawScenesMenu();
        DrawGraphicsMenu();
        DrawAboutMenu();

        DrawProjectSection();
        DrawSceneRenameSection();
        DrawPlaybackControls();
        DrawEngineStats();

        ImGui::EndMainMenuBar();

        DrawPopups();
    }

    void EditorLayer::DrawFileMenu()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMenu( "File" ) )
        {
            return;
        }

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
            m_SaveSceneRequested = true;
        }
        if ( ImGui::MenuItem( "Reload Scene", "CTRL+R" ) )
        {
        }

        DrawOpenSceneMenuItem();
        DrawStyleSubmenu();

        ImGui::Separator();

        if ( ImGui::MenuItem( "Rebuild Cooked Assets" ) )
        {
            RebuildCookedAssets();
        }

        ImGui::Separator();

        if ( ImGui::MenuItem( "Exit" ) )
        {
        }

        ImGui::EndMenu();
    }

    void EditorLayer::RebuildCookedAssets()
    {
        // Idle first: re-registering rebuilds GPU textures/materials.
        Graphic::Renderer::GetInstance().WaitDeviceIdle();

        if ( m_ImportManager )
            m_ImportManager->ImportAllFromDirectory( "Resources/Mesh/", /*force=*/true );

        if ( m_AssetPreloader )
            m_AssetPreloader->ReloadCooked();

        // Drop cached per-entity material instances so MeshECSSystem rebuilds them from the freshly
        // re-registered runtime materials (which now reference the reloaded texture images).
        if ( m_MainScene )
        {
            auto& reg = m_MainScene->GetRegistry();
            reg.view<ECS::StaticMeshComponent>().each( []( auto, ECS::StaticMeshComponent& c )
                                                       { c.RuntimeMaterialInstances.clear(); } );
            reg.view<ECS::SkinnedMeshComponent>().each( []( auto, ECS::SkinnedMeshComponent& c )
                                                        { c.RuntimeMaterialInstances.clear(); } );
        }

        if ( m_FileExplorerPanel )
            m_FileExplorerPanel->QueueRefresh();

        LOG_INFO( "[Editor] Rebuilt cooked assets" );
    }

    void EditorLayer::DrawStyleSubmenu()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMenu( "Style" ) )
        {
            return;
        }

        if ( ImGui::MenuItem( "Dark" ) )
        {
            ThemeManager::SetDarkTheme();
        }

        if ( ImGui::MenuItem( "Black" ) )
        {
            ThemeManager::SetBlackTheme();
        }

        ImGui::EndMenu();
    }

    void EditorLayer::DrawOpenSceneMenuItem()
    {
        namespace ImGui = ::ImGui;

        if ( ImGui::MenuItem( "Open Scene" ) )
        {
            PrepareScenePopup();
            m_OpenScenePopup = true;
        }
    }

    void EditorLayer::PrepareScenePopup()
    {
        m_AvailableScenes.clear();

        const auto scenePath = Common::Constants::Path::SCENE_PATH;

        for ( const auto& entry : std::filesystem::directory_iterator( scenePath ) )
        {
            if ( entry.path().extension() == Common::Constants::Extensions::SCENE_EXTENSION )
                m_AvailableScenes.push_back( entry.path() );
        }

        m_SelectedSceneIndex = -1;
    }

    void EditorLayer::DrawProjectSection()
    {
        namespace ImGui = ::ImGui;

        ImGui::PushFont( Editor::EditorResources::GetBoldFont() );

        ImGui::SameLine( ImGui::GetCursorPosX() + 40.0f );
        ImGui::Separator();
        ImGui::SameLine();

        static const std::filesystem::path currentPath = std::filesystem::current_path();
        static const std::string           projectName = currentPath.filename().string();
        ImGui::TextUnformatted( projectName.c_str() );

        Utils::ImGuiUtilities::Tooltip( currentPath.string().c_str() );

        ImGui::SameLine();
        ImGui::Separator();

        ImGui::PopFont();
    }

    void EditorLayer::DrawSceneRenameSection()
    {
        namespace ImGui = ::ImGui;

        static bool        renameScene = false;
        static std::string sceneNameBuffer;

        ImGui::SameLine( ImGui::GetCursorPosX() + 32.0f );

        if ( !renameScene )
        {
            ImGui::TextUnformatted( m_MainScene->GetSceneName().c_str() );

            if ( ImGui::IsItemHovered() )
            {
                ImGui::SetTooltip( "Double-click to rename the scene" );
                if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                {
                    renameScene     = true;
                    sceneNameBuffer = m_MainScene->GetSceneName();
                }
            }
        }
        else
        {
            ImGui::SetNextItemWidth( 200.0f );
            Utils::ImGuiUtilities::InputText( sceneNameBuffer, "##SceneRename" );

            if ( ImGui::IsItemDeactivatedAfterEdit() )
            {
                if ( !sceneNameBuffer.empty() )
                    m_MainScene->SetSceneName( sceneNameBuffer );

                renameScene = false;
            }

            if ( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
            {
                renameScene = false;
            }
        }
    }

    void EditorLayer::DrawPlaybackControls()
    {
        namespace ImGui = ::ImGui;

        ImGui::SameLine( ( ImGui::GetWindowContentRegionMax().x * 0.5f ) -
                         ( 1.5f * ( ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.x ) ) );

        DrawPlayButton();
        ImGui::SameLine();
        DrawPauseButton();
    }

    void EditorLayer::DrawEngineStats()
    {
        namespace ImGui = ::ImGui;

        const auto text = m_Application->GetEngineStats().GetFormattedStats();
        auto       size = ImGui::CalcTextSize( text.c_str() );

        ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - size.x - ImGui::GetStyle().ItemSpacing.x * 2.0f );

        ImGui::Text( text.c_str() );
    }

    void EditorLayer::DrawPopups()
    {
        DrawOpenScenePopup();
        DrawSaveScenePopup();
        DrawNewScenePopup();
        DrawReloadScenePopup();
        DrawProjectPopup();
    }

    void EditorLayer::DrawEditMenu()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMenu( "Edit" ) )
        {
            return;
        }

        ImGui::EndMenu();
    }

    void EditorLayer::DrawViewMenu()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMenu( "View" ) )
        {
            return;
        }

        for ( auto& panel : m_Panels )
        {
            ImGui::MenuItem( panel->GetName().c_str(), "", &panel->GetVisibility(), true );
        }

        ImGui::EndMenu();
    }

    void EditorLayer::LoadScene( const Common::Filepath& path )
    {
        m_SceneLoadRequested = path;
    }

    void EditorLayer::LoadSceneInternal( const Common::Filepath& path )
    {
        if ( !std::filesystem::exists( path ) )
        {
            LOG_ERROR( "Scene file does not exist: {0}", path.string() );
            return;
        }

        // Wait for GPU to be idle before destroying resources mid-frame
        EngineContext::GetInstance().GetDevice()->WaitIdle();

        m_MainScene->Clear();

        Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
        const std::string             content = Common::Utils::FileSystem::ReadFileContent( path );
        serializer.DeserializeFromJson( content );

        m_MainScene->Init();

        m_RenderRegistry.release();
        m_RenderRegistry = std::make_unique<Render::RenderRegistry>( m_MainScene );

        // Update recent scenes
        auto it = std::find( m_RecentScenes.begin(), m_RecentScenes.end(), path );
        if ( it != m_RecentScenes.end() )
        {
            m_RecentScenes.erase( it );
        }
        m_RecentScenes.insert( m_RecentScenes.begin(), path );

        if ( m_RecentScenes.size() > 5 )
        {
            m_RecentScenes.pop_back();
        }
    }

    void EditorLayer::DrawScenesMenu()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMenu( "Scenes" ) )
        {
            return;
        }

        if ( ImGui::MenuItem( "Load Scene..." ) )
        {
            PrepareScenePopup();
            m_OpenScenePopup = true;
        }

        if ( !m_RecentScenes.empty() )
        {
            ImGui::Separator();
            ImGui::TextDisabled( "Recent Scenes" );

            for ( const auto& path : m_RecentScenes )
            {
                std::string label = path.filename().string();
                if ( ImGui::MenuItem( label.c_str() ) )
                {
                    LoadScene( path );
                }
            }
        }

        ImGui::EndMenu();
    }

    void EditorLayer::DrawGraphicsMenu()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMenu( "Graphics" ) )
        {
            return;
        }

        ImGui::EndMenu();
    }

    void EditorLayer::DrawAboutMenu()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMenu( "About" ) )
        {
            return;
        }

        ImGui::TextUnformatted( "Desert Engine Editor" );
        ImGui::EndMenu();
    }

    void EditorLayer::DrawPlayButton()
    {
        namespace ImGui = ::ImGui;

        bool selected = m_EditorState == EditorState::Play;

        if ( selected )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetSelectedColor() );
        }

        if ( ImGui::Button( ICON_MDI_PLAY ) )
        {
            // TODO
        }

        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Play" );

        if ( selected )
            ImGui::PopStyleColor();
    }

    void EditorLayer::DrawPauseButton()
    {
        namespace ImGui = ::ImGui;

        bool selected = m_EditorState == EditorState::Paused;

        if ( selected )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetSelectedColor() );
        }

        if ( ImGui::Button( ICON_MDI_PAUSE ) )
        {
            // TODO
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

    void EditorLayer::DrawOpenScenePopup()
    {
        namespace ImGui = ::ImGui;

        if ( m_OpenScenePopup )
        {
            ImGui::OpenPopup( "Open Scene" );
            m_OpenScenePopup = false;
        }

        ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                 ImVec2( 0.5f, 0.5f ) );

        if ( ImGui::BeginPopupModal( "Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextUnformatted( "Select Scene" );
            ImGui::Separator();

            ImGui::BeginChild( "SceneList", ImVec2( 450, 300 ), true );

            for ( int i = 0; i < static_cast<int>( m_AvailableScenes.size() ); ++i )
            {
                const auto filename = m_AvailableScenes[i].filename().string();

                if ( ImGui::Selectable( filename.c_str(), m_SelectedSceneIndex == i ) )
                {
                    m_SelectedSceneIndex = i;
                }

                if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                {
                    m_SelectedSceneIndex = i;
                }
            }

            ImGui::EndChild();

            ImGui::Separator();

            if ( ImGui::Button( "Load", ImVec2( 120, 0 ) ) )
            {
                if ( m_SelectedSceneIndex >= 0 &&
                     m_SelectedSceneIndex < static_cast<int>( m_AvailableScenes.size() ) )
                {
                    LoadScene( m_AvailableScenes[m_SelectedSceneIndex] );
                }

                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void EditorLayer::DrawSaveScenePopup()
    {
        if ( !m_SaveSceneRequested )
        {
            return;
        }

        m_SaveSceneRequested = false;
        m_MainScene->Serialize( m_AssetManager.get() );
    }

    void EditorLayer::DrawNewScenePopup()
    {
    }

    void EditorLayer::DrawReloadScenePopup()
    {
    }

    void EditorLayer::DrawProjectPopup()
    {
    }

    void EditorLayer::OnEvent( Common::Event& event )
    {
#ifdef EBABLE_IMGUI
        for ( auto& panel : m_Panels )
        {
            if ( event.m_Handled )
                break;
            panel->OnEvent(event);
        }
#endif
    }

    Common::BoolResultStr EditorLayer::OnDetach()
    {
#ifdef EBABLE_IMGUI
        m_Panels.clear();
        m_ImGuiLayer->OnDetach();
        m_ImGuiLayer.reset();
#endif
        return BOOLSUCCESS;
    }

} // namespace Desert::Editor
