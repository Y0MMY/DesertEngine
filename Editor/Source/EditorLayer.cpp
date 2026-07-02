#define IMGUI_DEFINE_MATH_OPERATORS

#include "EditorLayer.hpp"
#include <Common/Core/Core.hpp>
#include <Common/Core/Profiler.hpp>
#include <Editor/Import/MeshDnD.hpp>

// 1. Engine Core
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Geometry/PrimitiveType.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/ProceduralCharacterFactory.hpp>
#include <Engine/Animation/ProceduralCharacterAnimations.hpp>
#include <Engine/Assets/Mesh/AnimationAsset.hpp>
#include <Engine/Scripting/ScriptEngine.hpp>
#include <Engine/Core/Serialize/SceneSerializer.hpp>

// 2. Editor Base & Infrastructure
#include "Editor/Core/EditorResources.hpp"
#include "Editor/Core/ThemeManager.hpp"
#include "Editor/Core/GizmoState.hpp"

#include <Engine/Graphic/Image.hpp> // Image2D::ReadPixelsRGBA8 (debug frame dump)
#include <Engine/Core/Input.hpp>
#include <Common/Core/KeyCodes.hpp>
#include <stb_image/stb_image_write.h>
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
#include "Editor/Panels/Collections/CollectionsPanel.hpp"

// 4. Misc
#include <glm/gtx/matrix_decompose.hpp>
#include <Engine/ECS/System/MeshECSSystem.hpp>
#include <Engine/ECS/System/SkyboxECSSystem.hpp>
#include <Engine/ECS/System/DayNightSystem.hpp>
#include <Engine/ECS/System/TerrainECSSystem.hpp>
#include <Engine/Graphic/Materials/DataDrivenMaterial.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Engine/ECS/System/PointLightSystem.hpp>
#include <Engine/ECS/System/SpotLightSystem.hpp>
#include <Engine/ECS/System/AnimationECSSystem.hpp>
#include <Engine/ECS/System/AttachmentSystem.hpp>
#include <Engine/ECS/System/PhysicsECSSystem.hpp>
#include <Engine/ECS/System/LocomotionSystem.hpp>
#include <Engine/ECS/System/ScriptSystem.hpp>

namespace Desert::Editor
{
    static constexpr uint32_t s_ShaderLibraryPanelIndex = 2;

    EditorLayer::EditorLayer( const Engine::Application* application, const std::string& layerName )
         : Common::Layer( layerName ), m_Application( application )

    {
        m_AssetManager = std::make_shared<Assets::AssetManager>();

        m_ImportManager = std::make_unique<ImportManager>();
        // Cook only what's missing/stale (skips the expensive Assimp re-parse on every launch). Collections
        // hold packs (a character + its animation FBXs), so they're cooked too — their outputs land under
        // Cooked/Meshes/Collections/... where the preloader discovers them (see CookPaths::CookedMesh).
        m_ImportManager->ImportAllFromDirectory( Common::Constants::Path::MESH_PATH );
        m_ImportManager->ImportAllFromDirectory( Common::Constants::Path::COLLECTIONS_PATH );

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

        // Day/night runs FIRST so the sun's direction + intensity are current before the sky and the mesh
        // lighting read the directional light this frame. No-op unless SceneSettings::EnableDayNight is on.
        m_MainScene->AddSystem<ECS::DayNightSystem>( m_MainScene.get() );
        m_MainScene->AddSystem<ECS::MeshECSSystem>();
        m_MainScene->AddSystem<ECS::SkyboxECSSystem>();
        m_MainScene->AddSystem<ECS::TerrainECSSystem>();
        m_MainScene->AddSystem<ECS::PointLightECSSystem>();
        m_MainScene->AddSystem<ECS::SpotLightECSSystem>();
        m_MainScene->AddSystem<ECS::AnimationECSSystem>( m_AnimationLibrary.get() );
        // AttachmentSystem runs right AFTER animation: weapons-in-hand follow the freshly-posed bone this frame.
        m_MainScene->AddSystem<ECS::AttachmentSystem>( m_MainScene.get() );
        // ScriptSystem runs BEFORE physics: scripts set the character's move intent (+ look) which
        // PhysicsECSSystem then executes the same frame.
        m_MainScene->AddSystem<ECS::ScriptSystem>( m_MainScene.get(), m_AssetManager.get() );
        m_MainScene->AddSystem<ECS::PhysicsECSSystem>( m_MainScene.get() );
        // Maps character movement state (speed/onGround from physics) -> locomotion clip. Kept OUT of physics
        // (mechanism vs behaviour); runs after it so it reads this frame's state.
        m_MainScene->AddSystem<ECS::LocomotionSystem>( m_MainScene.get() );

        // Persistent Cornell-Box GI + glass showcase (loads by default). Red/green walls bleed onto the white
        // objects (SSGI); a clear glass sphere sits in front of an orange cube (visible THROUGH the glass); a
        // point light sits BEHIND the objects. Uses the scene's existing sun (adding a 2nd directional light
        // overflows the single-light UB — see [[deferred-rendering-wip]]).
        {
            auto tinted = [&]( const char* name, glm::vec3 pos, glm::vec3 scale, glm::vec4 albedo )
            {
                auto& e = m_MainScene->CreateNewEntity( std::string( name ) );
                e.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Cube;
                auto& tf       = e.GetComponent<ECS::TransformComponent>();
                tf.Translation = pos;
                tf.Scale       = scale;
                auto& mc       = e.AddComponent<ECS::MaterialComponent>();
                mc.ShaderName  = "StaticMeshPBR";
                mc.Params.push_back( ECS::MaterialParamOverride{ "AlbedoColor", albedo } );
                mc.Params.push_back( ECS::MaterialParamOverride{ "RoughnessFactor", glm::vec4( 0.9f ) } );
            };
            const glm::vec4 white( 0.82f, 0.82f, 0.80f, 1 ), red( 0.85f, 0.10f, 0.10f, 1 ),
                 green( 0.10f, 0.70f, 0.15f, 1 );
            tinted( "CB_Floor", { 0, 0, 0 }, { 6, 0.2f, 6 }, white );
            tinted( "CB_Back", { 0, 3, -3 }, { 6, 6, 0.2f }, white );
            tinted( "CB_LeftRed", { -3, 3, 0 }, { 0.2f, 6, 6 }, red );
            tinted( "CB_RightGreen", { 3, 3, 0 }, { 0.2f, 6, 6 }, green );
            // Orange opaque cube directly behind the glass sphere (seen through it).
            tinted( "CB_OrangeCube", { 0, 1.3f, -1.2f }, { 1.4f, 1.4f, 1.4f }, glm::vec4( 0.95f, 0.5f, 0.08f, 1 ) );

            // Clear glass sphere in front of the cube.
            auto& glass = m_MainScene->CreateNewEntity( std::string( "CB_GlassSphere" ) );
            glass.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Sphere;
            auto& gtf       = glass.GetComponent<ECS::TransformComponent>();
            gtf.Translation = { 0.0f, 1.5f, 0.7f };
            gtf.Scale       = glm::vec3( 1.6f );
            auto& gmc       = glass.AddComponent<ECS::MaterialComponent>();
            gmc.ShaderName  = "StaticMeshPBR";
            gmc.Params.push_back( ECS::MaterialParamOverride{ "Transmission", glm::vec4( 0.9f ) } );
            gmc.Params.push_back( ECS::MaterialParamOverride{ "IOR", glm::vec4( 1.5f ) } );
            gmc.Params.push_back( ECS::MaterialParamOverride{ "GlassTint", glm::vec4( 0.75f, 0.9f, 1.0f, 1 ) } );

            // Point light BEHIND the objects (backlight / rim).
            auto& pl = m_MainScene->CreateNewEntity( std::string( "CB_BackLight" ) );
            auto& pld = pl.AddComponent<ECS::PointLightComponent>().Data;
            pld.Color     = glm::vec3( 1.0f, 0.85f, 0.6f );
            pld.Intensity = 8.0f;
            pld.Radius    = 12.0f;
            pl.GetComponent<ECS::TransformComponent>().Translation = { 0.0f, 2.5f, -2.5f };
        }

        const auto animations = m_AssetManager->FindAllByType<Assets::AnimationAsset>();

        for ( const auto& [handle, anim] : animations )
        {
            if ( !anim )
                continue;

            m_AnimationLibrary->Register( anim );
        }

        // Engine-level locomotion clips (idle/walk/run/jump) for the procedural humanoid — registered into the
        // AnimationLibrary so they show in the clip selector + AnimationECSSystem can auto-play. The editor
        // just invokes the engine helper (the locomotion knowledge lives in the engine, not here).
        Animation::ProceduralCharacterAnimations::RegisterClips( *m_AssetManager, *m_AnimationLibrary );

        m_MainScene->Init();

#ifdef EBABLE_IMGUI
        m_Panels.emplace_back( std::make_unique<Editor::SceneHierarchyPanel>( m_MainScene, m_AssetManager ) );
        m_Panels.emplace_back( std::make_unique<Editor::ScenePropertiesPanel>( m_MainScene, m_AssetManager,
                                                                               m_AnimationLibrary.get() ) );
        m_Panels.emplace_back( std::make_unique<Editor::ShaderLibraryPanel>() );
        m_Panels.emplace_back( std::make_unique<Editor::ViewportPanel>( m_MainScene, m_AssetManager.get() ) );
        {
            auto fileExplorer =
                 std::make_unique<Editor::FileExplorerPanel>( "Resources/Assets/", m_AssetManager.get(),
                                                              m_MainScene );
            m_FileExplorerPanel = fileExplorer.get();
            m_Panels.emplace_back( std::move( fileExplorer ) );
        }
        m_Panels.emplace_back( std::make_unique<Editor::MeshEditorPanel>( m_MainScene ) );
        m_Panels.emplace_back( std::make_unique<Editor::SceneSettingsPanel>( m_MainScene ) );
        m_Panels.emplace_back( std::make_unique<Editor::LogsPanel>() );
        m_Panels.emplace_back( std::make_unique<Editor::CollectionsPanel>( m_AssetManager.get() ) );
#endif // EBABLE_IMGUI

        m_RenderRegistry = std::make_unique<Render::RenderRegistry>( m_MainScene );

        // Boot into an empty "New Scene" — the demo scene (procedural character/house + player_controller.lua)
        // referenced assets that were cleared out for the from-scratch rebuild. Re-enable to get it back.
        // BuildCharacterDemoScene();

        // Default scene content: a sun + procedural sky (like UE's default level) so created meshes/primitives
        // are LIT and have a backdrop (an empty scene with no light renders everything ~black).
        {
            using namespace ::Desert;
            auto& sun = m_MainScene->CreateNewEntity( "Sun" );
            auto& dl  = sun.AddComponent<ECS::DirectionLightComponent>();
            dl.Data.Color     = { 1.0f, 0.97f, 0.9f };
            dl.Data.Intensity = 3.0f;
            sun.GetComponent<ECS::TransformComponent>().Translation = { -0.4f, -1.0f, -0.5f };

            auto& skyEnt   = m_MainScene->CreateNewEntity( "Skybox" );
            auto& sky      = skyEnt.AddComponent<ECS::SkyboxComponent>();
            sky.Procedural  = true;
            sky.RequestBake = true;
        }

        return BOOLSUCCESS;
    }

    [[nodiscard]] Common::BoolResultStr EditorLayer::OnUpdate( const Common::Timestep& ts )
    {
        DESERT_PROFILE_SCOPE( "Layer::OnUpdate" );

        if ( m_SceneLoadRequested )
        {
            auto path = m_SceneLoadRequested.value();
            m_SceneLoadRequested.reset();
            LoadSceneInternal( path );
        }

        // Stop is deferred here (between frames) so it never destroys/recreates render resources while a
        // command buffer that references them is in flight — see m_PendingSceneStop.
        if ( m_PendingSceneStop )
        {
            m_PendingSceneStop = false;
            OnSceneStop();
        }

        // Apply any deferred panel state (e.g. viewport resize) before scene rendering.
        // Panels defer GPU-side resize from OnUIRender to here so descriptor set pools are
        // never destroyed while their DS are bound to the recording command buffer.
        for ( auto& panel : m_Panels )
            panel->OnPreUpdate();

        Common::BoolResultStr beginResult = BOOLSUCCESS;
        {
            DESERT_PROFILE_SCOPE( "Scene::BeginScene" );
            beginResult = m_MainScene->BeginScene();
        }
        if ( !beginResult )
        {
            return Common::MakeError( beginResult.GetError() );
        }
        m_RenderRegistry->Render();

        {
            DESERT_PROFILE_SCOPE( "Scene::OnUpdate" );
            m_MainScene->OnUpdate( ts );
        }

        // DEBUG: press F9 to dump the final rendered viewport image to F:/DesertEngine/frame_dump.png. Useful
        // because external GDI/PrintWindow capture returns white for the Vulkan surface — this reads the actual
        // rendered frame back from the GPU. Edge-detected so one press = one dump.
        {
            static bool s_f9Prev = false;
            const bool  f9       = Input::Keyboard::IsKeyPressed( Common::KeyCode::F9 );
            if ( f9 && !s_f9Prev )
            {
                Graphic::Renderer::GetInstance().WaitDeviceIdle();
                if ( auto img = m_MainScene->GetFinalImage() )
                {
                    const std::vector<uint8_t> px = img->ReadPixelsRGBA8();
                    const uint32_t             w = img->GetWidth(), h = img->GetHeight();
                    if ( px.size() == static_cast<size_t>( w ) * h * 4 )
                    {
                        stbi_flip_vertically_on_write( 0 );
                        stbi_write_png( "F:/DesertEngine/frame_dump.png", w, h, 4, px.data(), w * 4 );
                        LOG_INFO( "[Dump] final frame -> frame_dump.png ({}x{})", w, h );
                    }
                }
            }
            s_f9Prev = f9;
        }


        Common::BoolResultStr endResult = BOOLSUCCESS;
        {
            DESERT_PROFILE_SCOPE( "Scene::EndScene" );
            endResult = m_MainScene->EndScene();
        }

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

        // Toolbar strip FIRST so it reserves its height at the top; the DockSpace below then fills the
        // remaining area (drawing it after a full-height DockSpace(0,0) would push the bar off-screen).
        DrawToolbar();

        // Submit the DockSpace
        ImGuiIO& io = ::ImGui::GetIO();

        if ( io.ConfigFlags & ImGuiConfigFlags_DockingEnable )
        {
            // Reserve the bottom status-bar height so the DockSpace fills only the area between the toolbar
            // and the status bar (a full-height DockSpace(0,0) would sit under the status bar).
            const float  statusBarHeight = ::ImGui::GetFrameHeight() + 4.0f;
            ImVec2       dockSize        = ::ImGui::GetContentRegionAvail();
            dockSize.y                   = ( dockSize.y > statusBarHeight ) ? dockSize.y - statusBarHeight : 0.0f;

            ImGuiID dockspace_id = ::ImGui::GetID( "MyDockSpace" );
            ::ImGui::DockSpace( dockspace_id, dockSize, dockspace_flags );
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
            {
                DESERT_PROFILE_SCOPE_DYNAMIC( panel->GetName().c_str() );
                panel->OnUIRender();
            }
            if ( panel->GetName() == "Scene###scene" )
            {
                ImGui::PopStyleVar();
            }
            ImGui::End();
        }

        DrawProfilerWindow();

        DrawStatusBar();

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
        // Play/Pause/Stop now live in the toolbar strip (DrawToolbar), not the menu bar.
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
        {
            m_ImportManager->ImportAllFromDirectory( Common::Constants::Path::MESH_PATH, /*force=*/true );
            m_ImportManager->ImportAllFromDirectory( Common::Constants::Path::COLLECTIONS_PATH, /*force=*/true );
        }

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

    void EditorLayer::DrawStatusBar()
    {
        namespace ImGui  = ::ImGui;
        using SceneState = ::Desert::Core::Scene::SceneState;

        const auto state = m_MainScene->GetState();
        const char* stateText = ( state == SceneState::Play )     ? ICON_MDI_PLAY " Play"
                                : ( state == SceneState::Paused ) ? ICON_MDI_PAUSE " Paused"
                                                                  : ICON_MDI_PENCIL " Edit";
        const ImVec4 stateColor =
             ( state == SceneState::Edit ) ? ThemeManager::GetIconColor() : ThemeManager::GetSelectedColor();

        ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.086f, 0.086f, 0.086f, 1.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f, 2.0f ) );
        ImGui::BeginChild( "##StatusBar", ImVec2( 0.0f, 0.0f ), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

        // Left: scene state + current selection.
        ImGui::PushStyleColor( ImGuiCol_Text, stateColor );
        ImGui::TextUnformatted( stateText );
        ImGui::PopStyleColor();

        ImGui::SameLine( 0.0f, 16.0f );
        if ( const auto sel = Core::SelectionManager::GetSelected() )
        {
            std::string selName = "Entity";
            if ( auto e = m_MainScene->FindEntityByID( *sel ) )
                selName = e->get().GetComponent<ECS::TagComponent>().Tag;
            ImGui::TextDisabled( ICON_MDI_CURSOR_DEFAULT_OUTLINE " %s", selName.c_str() );
        }
        else
        {
            ImGui::TextDisabled( "No selection" );
        }

        // Right: frame rate + frame time.
        const float fps    = ImGui::GetIO().Framerate;
        char        stats[64];
        std::snprintf( stats, sizeof( stats ), ICON_MDI_SPEEDOMETER " %.0f FPS   %.2f ms", fps,
                       fps > 0.0f ? 1000.0f / fps : 0.0f );
        const float statsW = ImGui::CalcTextSize( stats ).x;
        ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - statsW );
        ImGui::TextDisabled( "%s", stats );

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void EditorLayer::DrawToolbar()
    {
        namespace ImGui = ::ImGui;
        using Op        = ::Desert::Editor::Core::GizmoState;

        // A comfortably tall strip so its buttons read as a real toolbar, not menu-bar afterthoughts.
        const float barHeight = ImGui::GetFrameHeight() * 1.33f;

        ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.086f, 0.086f, 0.086f, 1.0f ) ); // #161616 strip
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f, 4.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 5.0f, 0.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 4.0f );
        ImGui::BeginChild( "##Toolbar", ImVec2( 0.0f, barHeight ), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

        // Buttons fill the FULL height of the toolbar (minus the child's vertical padding) so they occupy the
        // whole strip; slightly wider than tall for a chunky UE-style hit target.
        const float  btnH = ImGui::GetContentRegionAvail().y;
        const ImVec2 btnSize( btnH * 1.4f, btnH );

        // ── Left: transform-gizmo mode toggles (mirror the viewport's Q/W/E/R; highlight the active one). ──
        const auto modeButton = [&]( const char* icon, Op::Operation op, const char* tip )
        {
            const bool active = Op::Get() == op;
            if ( active )
                ImGui::PushStyleColor( ImGuiCol_Button, ThemeManager::GetSelectedColor() );
            if ( ImGui::Button( icon, btnSize ) )
                Op::Set( op );
            if ( active )
                ImGui::PopStyleColor();
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "%s", tip );
            ImGui::SameLine();
        };
        modeButton( ICON_MDI_CURSOR_DEFAULT_OUTLINE, Op::Operation::None, "Select (Q)" );
        modeButton( ICON_MDI_AXIS_ARROW, Op::Operation::Translate, "Move (W)" );
        modeButton( ICON_MDI_ROTATE_ORBIT, Op::Operation::Rotate, "Rotate (E)" );
        modeButton( ICON_MDI_ARROW_EXPAND_ALL, Op::Operation::Scale, "Scale (R)" );

        // ── Centre: playback (Play/Stop toggle + Pause), same tall button size. ──
        const float spacing    = ImGui::GetStyle().ItemSpacing.x;
        const float playbackW  = btnSize.x * 2.0f + spacing; // Play/Stop + Pause
        ImGui::SameLine( ImGui::GetWindowContentRegionMax().x * 0.5f - playbackW * 0.5f );
        DrawPlayButton( btnSize );
        ImGui::SameLine();
        DrawPauseButton( btnSize );

        ImGui::EndChild();
        ImGui::PopStyleVar( 3 );
        ImGui::PopStyleColor();
    }

    void EditorLayer::DrawProfilerWindow()
    {
        namespace ImGui = ::ImGui;
        auto&     prof  = ::Common::Profiling::Profiler::Get();

        if ( !m_ShowProfiler )
            return;

        const double frameMs = prof.LastFrameMs();
        const double fps     = frameMs > 0.0001 ? 1000.0 / frameMs : 0.0;

        ImGui::SetNextWindowSize( ImVec2( 420, 460 ), ImGuiCond_FirstUseEver );
        ImGui::SetNextWindowPos( ImVec2( 700, 120 ), ImGuiCond_FirstUseEver );
        if ( !ImGui::Begin( "Profiler", &m_ShowProfiler ) ) // X button clears m_ShowProfiler
        {
            ImGui::End();
            return;
        }

        ImGui::Checkbox( "Enabled", &prof.Enabled() );
        ImGui::SameLine();
        ImGui::Checkbox( "Sort by time", &prof.SortByTime() );

        ImGui::SetNextItemWidth( 160.0f );
        ImGui::SliderFloat( "Avg window (s)", &prof.AvgWindowSeconds(), 0.1f, 2.0f, "%.1f" );

        ImGui::Text( "Frame: %.3f ms  (%.0f FPS)   [avg]", frameMs, fps );
        ImGui::SameLine();
        if ( ImGui::Button( "Dump to Log" ) )
        {
            LOG_INFO( "[Profiler] Frame {:.3f} ms ({:.0f} FPS)", frameMs, fps );
            for ( const auto& s : prof.LastFrame() )
                LOG_INFO( "[Profiler]   {:<28} {:>8.3f} ms  x{}", s.Name, s.TotalMs, s.Calls );
        }

        ImGui::Separator();

        if ( ImGui::BeginTable( "##prof", 4,
                                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp ) )
        {
            ImGui::TableSetupColumn( "Scope" );
            ImGui::TableSetupColumn( "ms" );
            ImGui::TableSetupColumn( "%" );
            ImGui::TableSetupColumn( "calls" );
            ImGui::TableHeadersRow();

            for ( const auto& s : prof.LastFrame() )
            {
                const double pct = frameMs > 0.0001 ? ( s.TotalMs / frameMs ) * 100.0 : 0.0;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( s.Name.c_str() );
                ImGui::TableNextColumn();
                ImGui::Text( "%.3f", s.TotalMs );
                ImGui::TableNextColumn();
                // Tint hot scopes (>25% of the frame) red.
                if ( pct > 25.0 )
                    ImGui::TextColored( ImVec4( 1.0f, 0.45f, 0.35f, 1.0f ), "%.1f", pct );
                else
                    ImGui::Text( "%.1f", pct );
                ImGui::TableNextColumn();
                ImGui::Text( "%u", s.Calls );
            }
            ImGui::EndTable();
        }

        ImGui::End();
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

        ImGui::Separator();
        ImGui::MenuItem( "Profiler", "", &m_ShowProfiler, true );

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

        // reset (not release) — assigning a new unique_ptr already destroys the old; release() leaked it.
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

    void EditorLayer::DrawPlayButton( const ImVec2& size )
    {
        namespace ImGui    = ::ImGui;
        using SceneState   = ::Desert::Core::Scene::SceneState;
        const bool playing = m_MainScene->GetState() != SceneState::Edit;

        if ( playing )
            ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetSelectedColor() );

        // One toggle: Play when editing, Stop (restore the snapshot) when playing/paused.
        if ( ImGui::Button( playing ? ICON_MDI_STOP : ICON_MDI_PLAY, size ) )
        {
            if ( playing )
                m_PendingSceneStop = true; // deferred to OnUpdate (between frames) — see m_PendingSceneStop
            else
                OnScenePlay();
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( playing ? "Stop" : "Play" );

        if ( playing )
            ImGui::PopStyleColor();
    }

    void EditorLayer::DrawPauseButton( const ImVec2& size )
    {
        namespace ImGui  = ::ImGui;
        using SceneState = ::Desert::Core::Scene::SceneState;
        const bool paused = m_MainScene->GetState() == SceneState::Paused;
        const bool active = m_MainScene->GetState() != SceneState::Edit; // pause only matters while playing

        if ( !active )
            ImGui::BeginDisabled();
        if ( paused )
            ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetSelectedColor() );

        if ( ImGui::Button( ICON_MDI_PAUSE, size ) )
            OnScenePauseToggle();
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( paused ? "Resume" : "Pause" );

        if ( paused )
            ImGui::PopStyleColor();
        if ( !active )
            ImGui::EndDisabled();
    }

    namespace
    {
        // One static box = mesh (Cube primitive) + Box collider + Static body, as a child of `parent`.
        // The Cube primitive spans 2 units, so the visual size is 2*scale and the collider half-extents == scale
        // (matches the demo ground). Child colliders are placed at their WORLD pose by PhysicsECSSystem.
        void AddHousePart( ::Desert::Core::Scene* scene, ::Desert::ECS::Entity parent, const char* name,
                           const glm::vec3& localPos, const glm::vec3& scale )
        {
            using namespace ::Desert;
            auto& e = scene->CreateNewEntity( std::string( name ) );
            e.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Cube;
            auto& t       = e.GetComponent<ECS::TransformComponent>();
            t.Translation = localPos;
            t.Scale       = scale;
            auto& col           = e.AddComponent<ECS::ColliderComponent>();
            col.Data.Shape       = Physics::ShapeType::Box;
            col.Data.HalfExtents = scale; // 2-unit cube -> half-extents == scale
            e.AddComponent<ECS::RigidBodyComponent>().Data.Type = Physics::BodyType::Static;
            scene->Attach( parent, e );
        }
    }

    // Builds a walkable greybox HOUSE (floor-less; sits on the demo ground): 4 walls (front wall has a
    // doorway) + a flat roof, each a static collider so the character walks in through the door and is blocked
    // by walls. All parented under one "House" root (a ready prefab root). 2-unit-cube convention: dims = 2*scale.
    void EditorLayer::BuildHouse( const glm::vec3& origin )
    {
        using namespace ::Desert;

        // By VALUE: creating the wall children below reallocates the entity store; a reference would dangle.
        ECS::Entity house = m_MainScene->CreateNewEntity( "House" );
        house.GetComponent<ECS::TransformComponent>().Translation = origin;

        // Interior ~8x8 m, walls 3 m tall, 0.2 m thick. Half-sizes (= scale, since the cube is 2 units):
        AddHousePart( m_MainScene.get(), house, "Wall_Back", { 0.0f, 1.5f, -4.0f }, { 4.0f, 1.5f, 0.1f } );
        AddHousePart( m_MainScene.get(), house, "Wall_Left", { -4.0f, 1.5f, 0.0f }, { 0.1f, 1.5f, 4.0f } );
        AddHousePart( m_MainScene.get(), house, "Wall_Right", { 4.0f, 1.5f, 0.0f }, { 0.1f, 1.5f, 4.0f } );
        // Front wall with a centered doorway (1.2 m wide, 2.2 m tall): two side segments + a lintel above.
        AddHousePart( m_MainScene.get(), house, "Wall_FrontL", { -2.3f, 1.5f, 4.0f }, { 1.7f, 1.5f, 0.1f } );
        AddHousePart( m_MainScene.get(), house, "Wall_FrontR", { 2.3f, 1.5f, 4.0f }, { 1.7f, 1.5f, 0.1f } );
        AddHousePart( m_MainScene.get(), house, "Door_Lintel", { 0.0f, 2.6f, 4.0f }, { 0.6f, 0.4f, 0.1f } );
        // Flat roof (slight overhang).
        AddHousePart( m_MainScene.get(), house, "Roof", { 0.0f, 3.1f, 0.0f }, { 4.2f, 0.1f, 4.2f } );

        LOG_INFO( "[Demo] House built at ({}, {}, {}) — walk in through the +Z doorway.", origin.x, origin.y,
                  origin.z );
    }

    void EditorLayer::BuildCharacterDemoScene()
    {
        using namespace ::Desert;

        // --- Sun (directional light) — DirectionLight stores its DIRECTION in TransformComponent.Translation
        {
            auto& sun = m_MainScene->CreateNewEntity( "Sun" );
            auto& dl  = sun.AddComponent<ECS::DirectionLightComponent>();
            dl.Data.Color     = { 1.0f, 0.97f, 0.9f };
            dl.Data.Intensity = 3.0f;
            sun.GetComponent<ECS::TransformComponent>().Translation = { -0.4f, -1.0f, -0.5f }; // direction
        }

        // --- Ground: a flat static box the character stands on (mesh + Box collider + Static body)
        {
            auto& ground = m_MainScene->CreateNewEntity( "Ground" );
            ground.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Cube;
            auto& gt        = ground.GetComponent<ECS::TransformComponent>();
            gt.Translation  = { 0.0f, -0.5f, 0.0f }; // top surface at y = 0
            gt.Scale        = { 20.0f, 0.5f, 20.0f };
            auto& gcol      = ground.AddComponent<ECS::ColliderComponent>();
            gcol.Data.Shape       = Physics::ShapeType::Box;
            gcol.Data.HalfExtents = { 20.0f, 0.5f, 20.0f }; // matches the scaled cube (world units)
            ground.AddComponent<ECS::RigidBodyComponent>().Data.Type = Physics::BodyType::Static;
        }

        // --- A few static obstacle boxes to walk into / around
        for ( int i = 0; i < 3; ++i )
        {
            auto& box = m_MainScene->CreateNewEntity( "Obstacle" + std::to_string( i ) );
            box.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Cube;
            auto& bt       = box.GetComponent<ECS::TransformComponent>();
            bt.Translation = { -4.0f + i * 4.0f, 0.5f, -5.0f };
            auto& bcol     = box.AddComponent<ECS::ColliderComponent>();
            bcol.Data.Shape       = Physics::ShapeType::Box;
            bcol.Data.HalfExtents = { 0.5f, 0.5f, 0.5f };
            box.AddComponent<ECS::RigidBodyComponent>().Data.Type = Physics::BodyType::Static;
        }

        // --- Player: a Character Controller (the physics capsule). NO RigidBody/Collider — the controller
        // IS the physics. The player entity is left UNSCALED so its children (visual body + camera) don't
        // inherit a non-uniform scale (which would skew/displace a child camera and its gizmo). Starts above
        // the ground so it drops on Play. By VALUE: creating children below can reallocate the entity store.
        ECS::Entity player = m_MainScene->CreateNewEntity( "Player" );
        {
            auto& cc       = player.AddComponent<ECS::CharacterControllerComponent>();
            cc.Data.Radius = 0.3f;
            cc.Data.Height = 1.8f;
            // Move/jump/look speeds are the SCRIPT's Properties now (Details ▸ Script), not the controller.
            player.GetComponent<ECS::TransformComponent>().Translation = { 0.0f, 3.0f, 0.0f };
            // Movement + mouse-look are now a Lua SCRIPT (engine only executes the physics it asks for).
            {
                ECS::ScriptSlot slot;
                slot.ScriptPath = "Resources/Assets/Scripts/player_controller.lua";
                player.AddComponent<ECS::ScriptComponent>().Scripts.push_back( std::move( slot ) );
            }
        }

        // --- Visual body: a CHILD holding the procedural skinned HUMANOID (head/torso/2 arms/2 legs). Its
        // mesh origin is at the feet, so we drop it by the capsule half-height (~0.9) to stand on the capsule
        // bottom. An AnimationComponent is attached so it animates once idle/walk/run clips are registered.
        {
            auto& body = m_MainScene->CreateNewEntity( "PlayerBody" );
            body.AddComponent<ECS::SkinnedMeshComponent>().MeshHandle =
                 Geometry::ProceduralCharacterFactory::GetHumanoidMesh();
            body.AddComponent<ECS::AnimationComponent>();
            body.GetComponent<ECS::TransformComponent>().Translation = { 0.0f, -0.9f, 0.0f };
            m_MainScene->Attach( player, body );
        }

        // --- Camera: a CHILD of the (unscaled) player. Offset behind+above = 3rd person; move it to ~(0,
        // 0.7, 0) with rotation 0 for 1st person. Follows the player via the hierarchy (WORLD transform).
        {
            auto& cam = m_MainScene->CreateNewEntity( "PlayerCamera" );
            auto& cd  = cam.AddComponent<ECS::CameraComponent>();
            cd.Data.IsMainCamera = true;
            auto& ct       = cam.GetComponent<ECS::TransformComponent>();
            ct.Translation = { 0.0f, 1.5f, 7.0f };                   // behind (+Z) and above the player
            ct.Rotation    = { glm::radians( -10.0f ), 0.0f, 0.0f }; // look slightly down at the player
            m_MainScene->Attach( player, cam );
        }

        BuildHouse( { 12.0f, 0.0f, 0.0f } ); // a walkable greybox house off to the side

        LOG_INFO( "[Demo] Character demo scene built — press Play, then WASD to move + Space to jump." );
    }

    void EditorLayer::OnScenePlay()
    {
        using SceneState = ::Desert::Core::Scene::SceneState;
        if ( m_MainScene->GetState() != SceneState::Edit )
            return;
        // Snapshot the authored scene so Stop can restore it exactly (play-time edits are discarded).
        Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
        m_PlaySnapshot = serializer.SerializeToJson();
        m_MainScene->SetState( SceneState::Play );
        m_EditorState = EditorState::Play;
    }

    void EditorLayer::OnSceneStop()
    {
        using SceneState = ::Desert::Core::Scene::SceneState;
        if ( m_MainScene->GetState() == SceneState::Edit || m_PlaySnapshot.empty() )
            return;

        EngineContext::GetInstance().GetDevice()->WaitIdle();
        m_MainScene->Clear();

        Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
        serializer.DeserializeFromJson( m_PlaySnapshot );
        m_MainScene->Init();

        // reset (not release) — assigning a new unique_ptr already destroys the old; release() leaked it.
        m_RenderRegistry = std::make_unique<Render::RenderRegistry>( m_MainScene );

        m_MainScene->SetState( SceneState::Edit );
    }

    void EditorLayer::OnScenePauseToggle()
    {
        using SceneState = ::Desert::Core::Scene::SceneState;
        if ( m_MainScene->GetState() == SceneState::Play )
            m_MainScene->SetState( SceneState::Paused );
        else if ( m_MainScene->GetState() == SceneState::Paused )
            m_MainScene->SetState( SceneState::Play );
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
