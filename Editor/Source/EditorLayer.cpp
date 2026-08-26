#define IMGUI_DEFINE_MATH_OPERATORS

#include "EditorLayer.hpp"

#include <Editor/Widgets/ThumbnailService.hpp>
#include <Common/Core/Core.hpp>
#include <Common/Core/Profiler.hpp>
#include <Editor/Import/MeshDnD.hpp>

// 1. Engine Core
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Geometry/PrimitiveType.hpp>
#include <Common/Core/Units.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/ProceduralCharacterFactory.hpp>
#include <Engine/Animation/ProceduralCharacterAnimations.hpp>
#include <Engine/Assets/Mesh/AnimationAsset.hpp>
#include <Engine/Scripting/ScriptEngine.hpp>
#include <Engine/Core/Serialize/SceneSerializer.hpp>
#include "Editor/Core/CrashRecovery.hpp"
#include "Editor/Core/LayoutManager.hpp"
#include "Editor/Core/PanelRequests.hpp"
#include "Editor/Core/SceneOpenRequest.hpp"
#include "Editor/Core/ShotOptions.hpp"
#include "Editor/Core/MaterialAssetUtils.hpp"
#include <Engine/Assets/Prefab/PrefabAsset.hpp>
#include <Common/Utilities/FileSystem.hpp>

// 2. Editor Base & Infrastructure
#include "Editor/Core/EditorResources.hpp"
#include "Editor/Core/ThemeManager.hpp"
#include "Editor/Core/GizmoState.hpp"
#include "Editor/Core/CommandHistory.hpp"
#include "Editor/Core/Commands/SceneCommands.hpp"
#include "Editor/Core/EditorPreferences.hpp"
#include "Editor/Core/ProjectContext.hpp"

#include <Engine/Graphic/Image.hpp> // Image2D::ReadPixelsRGBA8 (debug frame dump)
#include <Engine/Core/Input.hpp>
#include <Common/Core/KeyCodes.hpp>
#include <Common/Core/Version.hpp>
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
#include "Editor/Panels/SceneSettings/SceneSettingsPanel.hpp"
#include "Editor/Panels/Modeling/ModelingPanel.hpp"
#include "Editor/Panels/Logs/LogsPanel.hpp"
#include "Editor/Panels/Collections/CollectionsPanel.hpp"
#include "Editor/Panels/NodeGraph/NodeGraphPanel.hpp"
#include "Editor/Panels/Animation/AnimGraphPanel.hpp"
#include "Editor/Panels/Photogrammetry/PhotogrammetryPanel.hpp"
#include "Editor/Panels/Particles/ParticleEditorPanel.hpp"
#include "Editor/Panels/UI/UIEditorPanel.hpp"
#include "Editor/Panels/AssetReferences/AssetReferencesPanel.hpp"
#include "Editor/Panels/LuaConsole/LuaConsolePanel.hpp"
#include "Editor/Panels/Stubs/SequencerPanel.hpp"
#include "Editor/Panels/Stubs/BuildSettingsPanel.hpp"
#include "Editor/Panels/History/HistoryPanel.hpp"
#include "Editor/Panels/Validation/SceneValidationPanel.hpp"
#include "Editor/Panels/Clouds/CloudModellingVolumePanel.hpp"
#include "Editor/Core/StartupOptions.hpp"
#include "Editor/Panels/Clouds/CloudLayoutPanel.hpp"
#include "Editor/Panels/Clouds/CloudNoiseVolumePanel.hpp"
#include "Editor/Panels/Clouds/CloudTypePanel.hpp"
#include "Editor/Panels/Animation/AnimLayersPanel.hpp"
#include "Editor/Core/ToastManager.hpp"

// 4. Misc
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <Engine/ECS/System/MeshECSSystem.hpp>
#include <Engine/ECS/System/TextECSSystem.hpp>
#include <Engine/ECS/System/SkyboxECSSystem.hpp>
#include <Engine/ECS/System/HeightFogECSSystem.hpp>
#include <Engine/ECS/System/VolumetricCloudECSSystem.hpp>
#include <Engine/ECS/System/TimeOfDayECSSystem.hpp>
#include <Engine/ECS/System/TerrainECSSystem.hpp>
#include <Engine/Graphic/Materials/DataDrivenMaterial.hpp>
#include <Editor/Core/Rigging/RigBuilder.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Engine/ECS/System/PointLightSystem.hpp>
#include <Engine/ECS/System/SpotLightSystem.hpp>
#include <Engine/ECS/System/AnimationECSSystem.hpp>
#include <Engine/ECS/System/AttachmentSystem.hpp>
#include <Engine/ECS/System/PhysicsECSSystem.hpp>
#include <Engine/ECS/System/LocomotionSystem.hpp>
#include <Engine/ECS/System/ScriptSystem.hpp>
#include <Engine/ECS/System/AudioECSSystem.hpp>

#include <algorithm> // std::sort / std::transform (scene list)
#include <cctype>    // std::tolower (scene filter)

namespace Desert::Editor
{
    static constexpr uint32_t s_ShaderLibraryPanelIndex = 2;

    // "Unsaved changes" marker: the CommandHistory revision at the last save/load. Compared against the
    // current revision for the status-bar dirty dot; reset wherever the scene is (re)loaded or saved.
    static uint64_t s_SavedRevision = 0;

    static bool s_ShowPreferences = false; // Edit -> Preferences... window

    // Icon shown before a panel's tab/title + its View-menu entry. Keyed by the panel's STABLE name
    // (GetName(), which is also the ImGui dock ID) so we never touch that ID.
    static const char* PanelIcon( const std::string& name )
    {
        if ( name == "Scene###scene" )
            return ICON_MDI_MONITOR;
        if ( name == "Scene Outliner" )
            return ICON_MDI_FILE_TREE;
        if ( name == "Details" )
            return ICON_MDI_TUNE;
        if ( name == "Assets" )
            return ICON_MDI_FOLDER_OUTLINE;
        if ( name == "Scene Settings" )
            return ICON_MDI_COG;
        if ( name == "Logs" )
            return ICON_MDI_TEXT_BOX_OUTLINE;
        if ( name == "History" )
            return ICON_MDI_HISTORY;
        if ( name == "Collections" )
            return ICON_MDI_SHAPE_OUTLINE;
        if ( name == "Sequencer" )
            return ICON_MDI_CHART_TIMELINE;
        if ( name == "Anim Layers" )
            return ICON_MDI_ANIMATION;
        if ( name == "Node Graph" )
            return ICON_MDI_GRAPH;
        if ( name == "Anim Graph" )
            return ICON_MDI_STATE_MACHINE;
        if ( name == "Model from Photos" )
            return ICON_MDI_CUBE_SCAN;
        if ( name == "Particle Editor" )
            return ICON_MDI_CREATION;
        if ( name == "UI Editor" )
            return ICON_MDI_VIEW_DASHBOARD;
        if ( name == "Lua Console" )
            return ICON_MDI_CONSOLE;
        if ( name == "Build Settings" )
            return ICON_MDI_HAMMER_WRENCH;
        if ( name == "Asset References" )
            return ICON_MDI_LINK_VARIANT;
        if ( name == "Scene Validation" )
            return ICON_MDI_CLIPBOARD_CHECK_OUTLINE;
        if ( name == "Shader Library" )
            return ICON_MDI_PALETTE;
        return ICON_MDI_VIEW_DASHBOARD; // sensible default for any future panel
    }

    // Composes "<icon>  <label>###<stable id>". The visible part gets the icon; the trailing ###<name>
    // keeps the ImGui window ID EXACTLY panel->GetName(), so saved dock layouts and every GetName()==...
    // lookup keep working unchanged.
    // A tool panel that only makes sense for a particular selection or mode opens itself when that
    // context appears and steps aside when it goes away — so the tab strip carries what the current work
    // needs instead of every panel at once. Opening one BY HAND pins it (explicit intent wins) until the
    // user closes it again; see IPanel::IsContextual.
    void EditorLayer::UpdateContextualPanels()
    {
        for ( auto& panel : m_Panels )
        {
            // An EXPLICIT request always wins and applies to every panel, contextual or not: a button in
            // Details ("Open in Sequencer", "Anim Graph", "Particle Editor") asked for this panel by name.
            // It pins it, exactly like ticking it in the View menu — the user asked, so nothing auto-closes it.
            switch ( Core::PanelRequests::Consume( panel->GetName() ) )
            {
                case Core::PanelRequests::Action::Open:
                    panel->GetVisibility() = true;
                    panel->Pinned()        = true;
                    m_FocusPanel           = panel->GetName();
                    break;

                // A drawer button is a switch, not a summons: pressing it again puts the panel away.
                case Core::PanelRequests::Action::Toggle:
                    panel->GetVisibility() = !panel->GetVisibility();
                    panel->Pinned()        = panel->GetVisibility();
                    if ( panel->GetVisibility() )
                        m_FocusPanel = panel->GetName();
                    break;

                case Core::PanelRequests::Action::None:
                    break;
            }

            if ( !panel->IsContextual() )
                continue;

            const bool relevant = panel->IsRelevant();
            bool&      visible  = panel->GetVisibility();

            // Pinning is set ONLY where the user actually asks for the panel (View menu / command
            // palette). Inferring it from "visible but not relevant" also fired on the very first frame
            // for a panel that merely starts visible, pinning it open forever.
            if ( relevant && !visible && !panel->Pinned() )
            {
                visible = true;
                m_ContextualShown.insert( panel.get() );
                m_FocusPanel = panel->GetName(); // bring it forward in whatever dock it lives
            }
            else if ( !relevant && visible && !panel->Pinned() )
            {
                visible = false;
                m_ContextualShown.erase( panel.get() );
            }
            else if ( !visible )
            {
                m_ContextualShown.erase( panel.get() );
                panel->Pinned() = false; // closed by hand -> stop pinning it open
            }
        }
    }

    static std::string PanelDisplayTitle( const std::string& name )
    {
        std::string label = name;
        if ( const auto pos = label.find( "###" ); pos != std::string::npos )
            label.erase( pos ); // visible part only (drop any existing ###id)
        return std::string( PanelIcon( name ) ) + "  " + label + "###" + name;
    }

    EditorLayer::EditorLayer( const Engine::Application* application, const std::string& layerName )
         : Common::Layer( layerName ), m_Application( application )

    {
        m_AssetManager = std::make_shared<Assets::AssetManager>();

        m_ImportManager = std::make_unique<ImportManager>();
        // Cook only what's missing/stale (skips the expensive Assimp re-parse on every launch). Collections
        // hold packs (a character + its animation FBXs), so they're cooked too — their outputs land under
        // Cooked/Meshes/Collections/... where the preloader discovers them (see CookPaths::CookedMesh).
        //
        // STAGED: this used to run inline here and froze the window for seconds before the first frame.
        // The stages now execute one-per-frame from OnUpdate while OnImGuiRender shows a loading overlay.
        // NOTE: shaders are NOT staged — they load synchronously in OnAttach, because the render systems
        // (MeshECSSystem's default PBR materials) resolve their shaders in their constructors.
        m_StartupStages.push_back(
             { "Cooking meshes...",
               [this] { m_ImportManager->ImportAllFromDirectory( Common::Constants::Path::MESH_PATH ); } } );
        m_StartupStages.push_back(
             { "Cooking collections...", [this]
               { m_ImportManager->ImportAllFromDirectory( Common::Constants::Path::COLLECTIONS_PATH ); } } );
        m_StartupStages.push_back( { "Preloading meshes...", [this] { m_AssetPreloader->PreloadMeshes(); } } );
        m_StartupStages.push_back(
             { "Preloading environments...", [this] { m_AssetPreloader->PreloadSkyboxes(); } } );
        m_StartupStages.push_back(
             { "Preloading cloud noise volumes...", [this] { m_AssetPreloader->PreloadCloudNoiseVolumes(); } } );
        // AFTER the volumes, always: a cloud type binds the volume it names the moment it is created, and
        // one created first would find nothing to bind and render with the default edge.
        m_StartupStages.push_back(
             { "Preloading cloud types...", [this] { m_AssetPreloader->PreloadCloudTypes(); } } );
        m_StartupStages.push_back(
             { "Preloading hero clouds...", [this] { m_AssetPreloader->PreloadCloudModellingVolumes(); } } );

        m_AssetPreloader   = std::make_unique<Assets::AssetPreloader>( m_AssetManager );
        m_AnimationLibrary = std::make_unique<Animation::AnimationLibrary>( m_AssetManager.get() );
        m_SceneRenderer    = std::make_unique<Graphic::SceneRenderer>();
        m_MainScene        = std::make_shared<Desert::Core::Scene>( "New Scene", m_SceneRenderer.get() );
        m_PrimaryScene     = m_MainScene; // the always-present document #-1 (see SetActiveScene)

        // The scene/asset-manager the undoable structural commands operate on (the scene OBJECT is reused
        // across loads — Clear() + deserialize — so this stays valid; the history itself is cleared on
        // load/Play/Stop instead).
        Commands::SetContext( m_MainScene.get(), m_AssetManager.get() );

        LOG_INFO( "[Editor] Desert Engine {} ({} branch)", Common::Version::Full(), Common::Version::Branch() );

        // User prefs (snap steps, camera speed, autosave) from ~/.desertengine/editor.json. Snap values
        // apply immediately; the camera speed is applied on the first frame (the camera exists by then).
        EditorPreferences::Load();

        // Sandbox one-time bake of the Cornell showcase to a loadable scene (File -> Open ->
        // CornellDemo.desce). Runs BEFORE the default-scene handling below and clears itself, so it
        // starts from and ends on an empty scene — it never fights the Starter scene the sandbox's
        // own DefaultScene generates next.
        if ( ProjectContext::HasProject() && ProjectContext::Current().Name == "Desert Sandbox" )
        {
            const auto demoPath = Common::Constants::Path::SCENE_PATH /
                                  ( "CornellDemo" + Common::Constants::Extensions::SCENE_EXTENSION );
            std::error_code ec;
            if ( !std::filesystem::exists( demoPath, ec ) )
            {
                m_MainScene->Clear();
                BuildCornellShowcase();
                SaveSceneTo( demoPath.generic_string() );
                m_MainScene->Clear();
                LOG_INFO( "[Editor] Baked the Cornell showcase -> {}", demoPath.string() );
            }
        }

        // Launched with --project (Project Hub): adopt the project's name and queue its default scene
        // (loaded through the normal deferred path on the first frame, when the renderer is ready).
        // A DefaultScene that does not exist yet (a FRESH project, sandbox included) is GENERATED: the
        // Starter playground built once and saved into the project — startup content is data, not code.
        // Screenshot mode names its own scene; it is the whole point of the flag.
        if ( const auto& shot = ShotOptions::Get(); !shot.Scene.empty() )
        {
            // In CAPTURE mode a `--scene` that is not there is fatal, not something to carry on past.
            // The scene loader already logs and leaves the current scene standing, which is right for an
            // editor and wrong for a capture: the run would go on to write PNGs named after the scene
            // that was asked for, holding the picture of a different one. That is worse than no evidence,
            // because it looks exactly like evidence. Interactive `--scene` keeps the old behaviour.
            if ( shot.Active() && !std::filesystem::exists( shot.Scene ) )
            {
                LOG_ERROR( "[Shot] --scene '{}' does not exist (looked from '{}'); refusing to capture a "
                           "different scene under that name.",
                           shot.Scene, std::filesystem::current_path().string() );
                std::exit( 2 );
            }
            LoadScene( Common::Filepath( shot.Scene ) );
        }
        else if ( ProjectContext::HasProject() )
        {
            m_MainScene->SetSceneName( ProjectContext::Current().Name );
            if ( const auto scenePath = ProjectContext::DefaultScenePath(); !scenePath.empty() )
            {
                if ( std::filesystem::exists( scenePath ) )
                    LoadScene( scenePath );
                else
                {
                    BuildStarterScene();
                    SaveSceneTo( scenePath );
                    LOG_INFO( "[Editor] Generated the Starter scene -> {}", scenePath );
                }
            }
        }

        BuiltinMeshRegistry::Init( nullptr );

        // Crash recovery: if the previous session left its lock behind (unclean exit) and an autosave
        // exists, arm a prompt to reopen it. Then (re)arm the lock for THIS session; a clean shutdown
        // (OnDetach) removes it.
        if ( CrashRecovery::WasUncleanExit() )
        {
            m_RecoveryAutosave   = CrashRecovery::LatestAutosave();
            m_ShowRecoveryPrompt = !m_RecoveryAutosave.empty();
        }
        CrashRecovery::ArmSession();

        // LoadScene( "Resources/Assets/Scene/HouseDemo.desce" );
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

        // Shaders must exist BEFORE the render systems below are constructed (their default materials
        // resolve shaders in the ctor). Meshes/skyboxes are staged behind the loading overlay instead.
        m_AssetPreloader->PreloadShaders();

        BuildSceneSystems( *m_MainScene );

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
        {
            auto primaryViewport = std::make_unique<Editor::ViewportPanel>( m_MainScene, m_AssetManager.get() );
            // Focusing the main viewport rebinds the editor back to the primary scene (index -1).
            primaryViewport->SetOnActivate( [this] { SetActiveScene( -1 ); } );
            m_Panels.emplace_back( std::move( primaryViewport ) );
        }
        {
            auto fileExplorer = std::make_unique<Editor::FileExplorerPanel>( Common::Constants::Path::ASSETS_PATH,
                                                                             m_AssetManager.get(), m_MainScene );
            m_FileExplorerPanel = fileExplorer.get();
            m_Panels.emplace_back( std::move( fileExplorer ) );
        }
        m_Panels.emplace_back( std::make_unique<Editor::ModelingPanel>( m_MainScene ) );
        m_Panels.emplace_back( std::make_unique<Editor::SceneSettingsPanel>( m_MainScene ) );
        m_Panels.emplace_back( std::make_unique<Editor::LogsPanel>() );
        m_Panels.emplace_back( std::make_unique<Editor::CollectionsPanel>( m_AssetManager.get() ) );
        m_Panels.emplace_back( std::make_unique<Editor::HistoryPanel>() );
        m_Panels.emplace_back(
             std::make_unique<Editor::SceneValidationPanel>( m_MainScene, m_AssetManager.get() ) );
        m_Panels.emplace_back( std::make_unique<Editor::CloudNoiseVolumePanel>( m_AssetManager.get() ) );
        m_Panels.emplace_back( std::make_unique<Editor::CloudTypePanel>( m_AssetManager.get() ) );
        m_Panels.emplace_back( std::make_unique<Editor::CloudModellingVolumePanel>( m_AssetManager.get() ) );
        m_Panels.emplace_back( std::make_unique<Editor::CloudLayoutPanel>( m_MainScene, m_AssetManager.get() ) );

        // Visual stubs for upcoming tools (hidden by default; toggled via the View menu). No real
        // functionality yet — they exist so the layouts/interactions can be iterated on early.
        m_Panels.emplace_back( std::make_unique<Editor::NodeGraphPanel>( m_AssetManager ) );
        m_Panels.emplace_back( std::make_unique<Editor::AnimGraphPanel>( m_MainScene, m_AnimationLibrary.get() ) );
        m_Panels.emplace_back(
             std::make_unique<Editor::PhotogrammetryPanel>( m_MainScene, m_AssetManager.get() ) );
        m_Panels.emplace_back( std::make_unique<Editor::ParticleEditorPanel>( m_MainScene ) );
        m_Panels.emplace_back( std::make_unique<Editor::UIEditorPanel>( m_MainScene ) );
        m_Panels.emplace_back( std::make_unique<Editor::AssetReferencesPanel>( m_MainScene, m_AssetManager ) );
        m_Panels.emplace_back(
             std::make_unique<Editor::LuaConsolePanel>( m_MainScene.get(), m_AssetManager.get() ) );
        m_Panels.emplace_back( std::make_unique<Editor::SequencerPanel>( m_MainScene, m_AnimationLibrary.get(),
                                                                         m_AssetManager.get() ) );
        m_Panels.emplace_back(
             std::make_unique<Editor::AnimLayersPanel>( m_MainScene, m_AnimationLibrary.get() ) );
        m_Panels.emplace_back( std::make_unique<Editor::BuildSettingsPanel>() );

        // `--open-panel <name>`: put a tool on screen at boot, by the name the View menu shows. See
        // Editor/Core/StartupOptions.hpp for why it is an argument rather than a click. A CONTEXTUAL
        // panel is also PINNED, because opening one by hand is what pinning means and a name on the
        // command line is as deliberate as a menu tick.
        for ( const std::string& wanted : Editor::StartupOptions::Get().PanelsToOpen )
        {
            bool found = false;
            for ( auto& panel : m_Panels )
            {
                if ( panel->GetName() != wanted )
                    continue;
                panel->GetVisibility() = true;
                if ( panel->IsContextual() )
                    panel->Pinned() = true;
                found = true;
                break;
            }

            if ( !found )
            {
                // NAMED, WITH THE LIST. A flag that quietly did nothing is indistinguishable from a panel
                // that failed to draw, and this argument exists precisely for runs nobody is watching.
                std::string known;
                for ( auto& panel : m_Panels )
                    known += ( known.empty() ? "" : ", " ) + panel->GetName();
                LOG_ERROR( "[Editor] --open-panel '{}' names no panel. Known panels: {}", wanted, known );
            }
        }
#endif // EBABLE_IMGUI

        m_RenderRegistry = std::make_unique<Render::RenderRegistry>( m_MainScene );

        // Boot into an empty "New Scene" — the demo scene (procedural character/house + player_controller.lua)
        // referenced assets that were cleared out for the from-scratch rebuild. Re-enable to get it back.
        // BuildCharacterDemoScene();

        // Default scene content: a sun + procedural sky (like UE's default level) so created meshes/primitives
        // are LIT and have a backdrop (an empty scene with no light renders everything ~black).
        // ONLY for a genuinely empty boot: the constructor already gave a fresh project its Starter
        // scene (own sun+sky) and queued any existing project scene for load (brings its own). Adding
        // a sun here regardless is what produced TWO directional lights — and the engine supports one.
        const bool sceneLoadPending = m_SceneLoadRequested.has_value();
        const bool hasSun           = !m_MainScene->GetRegistry().view<ECS::DirectionLightComponent>().empty();
        if ( !sceneLoadPending && !hasSun )
        {
            using namespace ::Desert;
            auto& sun         = m_MainScene->CreateNewEntity( "Sun" );
            auto& dl          = sun.AddComponent<ECS::DirectionLightComponent>();
            dl.Data.Color     = { 1.0f, 0.97f, 0.9f };
            dl.Data.Intensity = 3.0f;
            sun.GetComponent<ECS::TransformComponent>().Translation = { -0.4f, -1.0f, -0.5f };

            auto& skyEnt    = m_MainScene->CreateNewEntity( "Skybox" );
            auto& sky       = skyEnt.AddComponent<ECS::SkyAtmosphereComponent>();
            sky.RequestBake = true;
        }

        return BOOLSUCCESS;
    }

    [[nodiscard]] Common::BoolResultStr EditorLayer::OnUpdate( const Common::Timestep& ts )
    {
        DESERT_PROFILE_SCOPE( "Layer::OnUpdate" );

        // Staged startup loading: run ONE heavy stage per frame — but only after at least one frame with
        // the loading overlay has been PRESENTED (else the first cook would freeze a blank window anyway).
        // While loading, the scene is NOT rendered at all (shaders/assets aren't there yet — rendering
        // before the preload stage crashed on the missing StaticMeshPBR shader); the frame is ImGui-only.
        if ( StartupLoading() )
        {
            if ( m_StartupFramesRendered >= 1 )
            {
                DESERT_PROFILE_SCOPE( "Startup stage" );
                m_StartupStages[m_StartupNext].Run();
                ++m_StartupNext;
            }
            return BOOLSUCCESS;
        }

        // The timestep this frame is driven by. Identical to the wall-clock one the application measured,
        // EXCEPT under a `--play` capture, where it is the fixed step from ShotOptions.
        //
        // Substituted for the whole layer update rather than only for the scene: a capture is reproducible
        // only if nothing in it integrates a number that came from a clock, and "the scene is deterministic
        // but the thing above it is not" is the kind of split that holds until the day something above it
        // starts feeding the scene. Outside `--play` this is `ts` itself, so no existing frame moves.
        const Common::Timestep frameTs( ShotOptions::Get().FrameSeconds( ts.GetSeconds() ) );

        // A scene handed over by a panel (dropped on the viewport, double-clicked in the asset browser).
        // It goes through the SAME deferred load as the menu — but a drag is easy to do by accident, so
        // unsaved work is not thrown away silently: the confirm popup decides, and only then do we queue.
        if ( auto requested = Editor::Core::SceneOpenRequest::Consume() )
        {
            const Common::Filepath path( *requested );
            if ( CommandHistory::Get().Revision() != s_SavedRevision )
            {
                m_PendingOpenScene      = path;
                m_ConfirmOpenScenePopup = true;
            }
            else
            {
                LoadScene( path );
            }
        }

        // Scene loads wait until the startup stages finished (a scene expects cooked/preloaded assets).
        if ( m_SceneLoadRequested && !StartupLoading() )
        {
            auto path = m_SceneLoadRequested.value();
            m_SceneLoadRequested.reset();
            LoadSceneInternal( path );
        }

        // New (empty) scene — deferred like a load so it never tears down resources mid-frame.
        if ( m_NewSceneRequested && !StartupLoading() )
        {
            m_NewSceneRequested = false;
            NewSceneInternal();
        }

        // Opening an extra scene view allocates a fresh SceneRenderer + Init() (WaitDeviceIdle + framebuffer
        // creation) — deferred here, between frames, for the same reason as scene load/stop above.
        if ( m_AddSceneViewRequested && !StartupLoading() )
        {
            m_AddSceneViewRequested = false;
            AddSceneView();
        }

        // Stop is deferred here (between frames) so it never destroys/recreates render resources while a
        // command buffer that references them is in flight — see m_PendingSceneStop.
        if ( m_PendingSceneStop )
        {
            m_PendingSceneStop = false;
            OnSceneStop();
        }

        // First-frame prefs application (needs a live camera) + autosave timer.
        {
            static bool s_CameraSpeedApplied = false;
            if ( !s_CameraSpeedApplied )
            {
                if ( auto cam = m_MainScene->GetMainCamera().lock() )
                    if ( auto* editorCam = dynamic_cast<::Desert::Core::EditorCamera*>( cam.get() ) )
                    {
                        editorCam->SetMovementSpeed( EditorPreferences::Get().CameraSpeed );
                        s_CameraSpeedApplied = true;
                    }
            }

            // Autosave: Edit mode only, only when something actually changed since the last autosave.
            // Writes a SEPARATE file (Scene/Autosave/<name>_autosave.desce) — never touches the main save.
            static float    s_AutosaveAccum        = 0.0f;
            static uint64_t s_LastAutosaveRevision = 0;
            const auto&     prefs                  = EditorPreferences::Get();
            if ( prefs.AutosaveMinutes > 0 && m_MainScene->GetState() == ::Desert::Core::Scene::SceneState::Edit )
            {
                s_AutosaveAccum += frameTs.GetSeconds();
                if ( s_AutosaveAccum >= static_cast<float>( prefs.AutosaveMinutes ) * 60.0f )
                {
                    s_AutosaveAccum    = 0.0f;
                    const uint64_t rev = CommandHistory::Get().Revision();
                    if ( rev != s_LastAutosaveRevision )
                    {
                        s_LastAutosaveRevision = rev;
                        Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
                        std::string                   name = m_MainScene->GetSceneName();
                        for ( auto& ch : name )
                            if ( ch == ' ' )
                                ch = '_';
                        const auto      dir = Common::Constants::Path::SCENE_PATH / "Autosave";
                        std::error_code ec;
                        std::filesystem::create_directories( dir, ec );
                        const auto path =
                             dir / ( name + "_autosave" + Common::Constants::Extensions::SCENE_EXTENSION );
                        Common::Utils::FileSystem::WriteContentToFile( path, serializer.SerializeToJson() );
                        LOG_INFO( "[Autosave] {}", path.string() );
                    }
                }
            }
        }

        // Apply any deferred panel state (e.g. viewport resize) before scene rendering.
        // Panels defer GPU-side resize from OnUIRender to here so descriptor set pools are
        // never destroyed while their DS are bound to the recording command buffer.
        for ( auto& panel : m_Panels )
            panel->OnPreUpdate();

        // ONE thumbnail capture pump for the whole editor. Panels only request; whether the asset browser
        // is open, hidden or closed no longer changes whether previews progress, and a request made by one
        // panel is finished for all of them.
        ThumbnailService::Get().Tick();

        UpdateContextualPanels();

        // Asset hot-reload: pick up edited .demat/.shader files (runs BEFORE scene rendering so
        // a shader-triggered pipeline invalidation never touches an in-recording frame).
        if ( m_AssetManager )
            m_AssetHotReload.Tick( frameTs, *m_AssetManager, m_MainScene.get() );

        // Destroy invalidated runtime materials (shader switched in the editor / hot reload) at
        // the only safe point: before any command recording, behind a device-idle wait. Doing it
        // where Invalidate() is called (mid-UI, mid-recording) kills descriptor pools the current
        // command buffer references -> device lost.
        if ( auto* materialService = Runtime::ResourceRegistry::GetMaterialService() )
            materialService->CollectGarbage();

        // Advance + upload any playing UI videos at this safe point (behind the device-idle wait, before
        // command recording) so authored videos animate live in the viewport; SetData flushes its own
        // transfer, and the UI walk later just samples the freshly-updated frame texture.
        if ( auto* videoService = Runtime::ResourceRegistry::GetVideoService() )
            videoService->UpdateAll();

        // Screenshot mode, `--play`: start the world before the first frame that will be counted.
        //
        // Through OnScenePlay(), the same entry the toolbar's Play button uses, so a headless run is a Play
        // session and not a second definition of one — the snapshot it takes is what would let a Stop
        // restore the authored scene, and a capture that entered Play by some private shortcut would drift
        // from the editor the day either changed.
        //
        // The camera is PINNED first, and that is the whole reason this block is not one line. Play hands
        // the view to the scene's own CameraComponent (Scene::UpdateActiveCameraSource), which would take
        // the shot away from `--camera`/`--look` in any scene that has a camera entity — and take it
        // SILENTLY, because the placement below asks for an EditorCamera and would simply not find one.
        // Pinning is the engine's existing "this view is driven from outside" mechanism and headless
        // capture is exactly that case, so `--play` changes what MOVES in the frame and nothing about
        // where the frame is taken from.
        if ( auto& shot = ShotOptions::Get(); shot.PlayActive() && !m_SceneLoadRequested && !StartupLoading() &&
                                              m_MainScene &&
                                              m_MainScene->GetState() == ::Desert::Core::Scene::SceneState::Edit )
        {
            if ( m_MainScene->GetActiveCamera() )
            {
                m_MainScene->PinActiveCamera( m_MainScene->GetActiveCamera() );
                OnScenePlay();
                LOG_INFO( "[Shot] --play: gameplay running at a fixed {} s step; the {} captured frames are "
                          "{} s of simulated time",
                          ShotOptions::PlayStepSeconds, shot.Frames, shot.SimulatedSeconds( shot.Frames ) );
            }
            else
            {
                // Refused rather than played anyway: with nothing to pin, Play would pick a view of its own
                // and the capture would answer a question about a pose nobody asked for — while looking
                // exactly like a legitimate result.
                LOG_ERROR( "[Shot] --play refused: scene '{}' has no active camera to pin, and Play would "
                           "choose the view itself. No gameplay time advanced; this capture is a frozen "
                           "world.",
                           m_MainScene->GetSceneName() );
                shot.Play = false;
            }
        }

        // Screenshot mode, FIRST HALF: place the camera for the frame that is about to be rendered.
        //
        // Before the render and not after it, because the capture below reads back whatever the render
        // produced: with the placement after it, the image written as frame N was rendered from the pose
        // of frame N-2, and on a MOVING path that is not a bookkeeping detail — a 120-degree pan over 90
        // frames puts the last captured frame 1.35 degrees, about 28 pixels, short of the endpoint the
        // command line named. Frame N is rendered from pose N, and the final frame lands exactly on
        // `--camera-to` / `--look-to`.
        //
        // With `--camera-to` / `--look-to` the pose is re-placed EVERY frame, walking the path across
        // exactly the warm-up frames. Without them `HasMotion()` is false, the placement happens once at
        // parameter 0, and the pose it computes is (Position, Forward) to the bit.
        if ( auto& shot = ShotOptions::Get(); shot.Active() && shot.HasCamera && !m_SceneLoadRequested &&
                                              !StartupLoading() && ( !m_ShotCameraPlaced || shot.HasMotion() ) )
        {
            if ( auto* cam = dynamic_cast<::Desert::Core::EditorCamera*>( m_MainScene->GetActiveCamera().get() ) )
            {
                const ShotCamera view    = shot.CameraAt( shot.Parameter( m_ShotFrame ) );
                const glm::vec3  forward = glm::normalize( view.Forward );
                cam->SnapToDirection( forward );
                // Focus keeps the orientation and re-frames, so aiming at a point one framing distance
                // ahead lands the camera exactly on the position asked for.
                cam->Focus( view.Position + forward * 500.0f, 500.0f );
                cam->SetInputEnabled( false ); // nothing may nudge it between here and the capture
            }
            m_ShotCameraPlaced = true;
        }

        // Multi-scene editing: drive EVERY open document each frame so all viewports render live. The active
        // one is m_MainScene (rebound on viewport focus); RigBuilder / F9 below act on it only. The outline
        // aid + Begin/RegistryRender/OnUpdate/End are folded into UpdateSceneFrame (see below), applied per
        // scene so a secondary viewport is a full, independent render — not a static snapshot.
        if ( auto r = UpdateSceneFrame( *m_PrimaryScene, m_RenderRegistry.get(), frameTs ); !r )
            return Common::MakeError( r.GetError() );
        for ( auto& doc : m_ExtraScenes )
            if ( auto r = UpdateSceneFrame( *doc->Scene, doc->Registry.get(), frameTs ); !r )
                return Common::MakeError( r.GetError() );

        // Runs a queued "Convert to Skinned" (rig builder) here, outside ImGui component iteration — the swap
        // removes the StaticMeshComponent the Details panel is drawing, so it must not happen mid-render.
        if ( m_MainScene && m_AssetManager )
            RigBuilder::ProcessPending( *m_MainScene, *m_AssetManager );

        // Screenshot mode, SECOND HALF: the frame just rendered is the frame that gets written. The frame
        // count is not decoration — a temporally accumulating pass needs several frames to converge, so an
        // early shot is a picture of the dither rather than of the scene.
        if ( auto& shot = ShotOptions::Get(); shot.Active() && !m_SceneLoadRequested && !StartupLoading() )
        {
            ++m_ShotFrame;

            // The sequence counts RENDERED frames, so `frame_00001` is the first frame rendered, from path
            // parameter 0, and `frame_000NN` at --shot-frames NN is the last, from parameter 1. When
            // `--shot-every` divides `--shot-frames` the last file of the sequence and the `--shot` PNG are
            // the same image — a cheap invariant to check a capture against.
            if ( !shot.Sequence.empty() && ( m_ShotFrame % shot.SequenceEvery ) == 0 )
            {
                char name[64];
                std::snprintf( name, sizeof( name ), "/frame_%05d.png", m_ShotFrame );
                const std::string path = shot.Sequence + name;
                if ( !WriteViewportPng( path ) )
                    LOG_ERROR( "[Shot] sequence frame {} not written to '{}'", m_ShotFrame, path );
            }

            if ( m_ShotFrame >= shot.Frames )
            {
                if ( !shot.Output.empty() && !WriteViewportPng( shot.Output ) )
                    LOG_ERROR( "[Shot] the final frame was not captured to '{}'", shot.Output );
                if ( shot.GpuProfile )
                    DumpProfilerToLog();
                const_cast<Engine::Application*>( m_Application )->Close();
            }
        }

        // DEBUG: press F9 to dump the final rendered viewport image to F:/DesertEngine/frame_dump.png. Useful
        // because external GDI/PrintWindow capture returns white for the Vulkan surface — this reads the actual
        // rendered frame back from the GPU. Edge-detected so one press = one dump.
        {
            static bool s_f9Prev = false;
            const bool  f9       = Input::Keyboard::IsKeyPressed( Common::KeyCode::F9 );
            if ( f9 && !s_f9Prev )
            {
                if ( !WriteViewportPng( "F:/DesertEngine/frame_dump.png" ) )
                    LOG_ERROR( "[Dump] final frame could not be written" );
            }
            s_f9Prev = f9;
        }

        return BOOLSUCCESS;
    }

    // Read the resolved viewport back off the GPU and write it as a PNG. The one place that does this: the
    // still capture, every frame of a `--shot-sequence`, and the F9 dump all go through here, so a capture
    // cannot quietly differ from a dump in flip, format or the device-idle wait that makes the readback
    // legal at all.
    bool EditorLayer::WriteViewportPng( const std::string& path )
    {
        if ( !m_MainScene )
        {
            LOG_ERROR( "[Shot] no scene to capture ('{}')", path );
            return false;
        }

        // The directory of a sequence is named on the command line and usually does not exist yet. Create
        // it rather than letting stb fail on a path that is only missing a folder.
        const std::filesystem::path file = std::filesystem::path( path );
        if ( file.has_parent_path() && !file.parent_path().empty() )
        {
            std::error_code ec;
            std::filesystem::create_directories( file.parent_path(), ec );
            if ( ec && !std::filesystem::exists( file.parent_path() ) )
            {
                LOG_ERROR( "[Shot] could not create '{}': {}", file.parent_path().string(), ec.message() );
                return false;
            }
        }

        Graphic::Renderer::GetInstance().WaitDeviceIdle();

        auto img = m_MainScene->GetFinalImage();
        if ( !img )
        {
            LOG_ERROR( "[Shot] scene has no final image ('{}')", path );
            return false;
        }

        const std::vector<uint8_t> px = img->ReadPixelsRGBA8();
        const uint32_t             w  = img->GetWidth();
        const uint32_t             h  = img->GetHeight();
        if ( px.size() != static_cast<size_t>( w ) * h * 4 )
        {
            LOG_ERROR( "[Shot] readback is {} bytes, expected {}x{}x4 = {}", px.size(), w, h,
                       static_cast<size_t>( w ) * h * 4 );
            return false;
        }

        stbi_flip_vertically_on_write( 0 );
        const bool written = stbi_write_png( path.c_str(), w, h, 4, px.data(), w * 4 ) != 0;
        LOG_INFO( "[Shot] {} -> {} ({}x{})", written ? "wrote" : "FAILED to write", path, w, h );
        return written;
    }

    void EditorLayer::BuildSceneSystems( Desert::Core::Scene& scene )
    {
        scene.AddSystem<ECS::MeshECSSystem>();
        scene.AddSystem<ECS::TextECSSystem>();
        // BEFORE the collectors: it writes the atmosphere sun's transform, which the sky collector, the
        // light collector and the shadow path all read this same frame.
        scene.AddSystem<ECS::TimeOfDayECSSystem>();
        scene.AddSystem<ECS::SkyboxECSSystem>();
        // A pure render-data collector: reads the fog component (and its entity's transform Y, the fog
        // floor) and emits one command.
        scene.AddSystem<ECS::HeightFogECSSystem>();
        scene.AddSystem<ECS::VolumetricCloudECSSystem>();
        scene.AddSystem<ECS::TerrainECSSystem>();
        scene.AddSystem<ECS::PointLightECSSystem>();
        scene.AddSystem<ECS::SpotLightECSSystem>();
        scene.AddSystem<ECS::AnimationECSSystem>( m_AnimationLibrary.get() );
        // AttachmentSystem runs right AFTER animation: weapons-in-hand follow the freshly-posed bone this frame.
        scene.AddSystem<ECS::AttachmentSystem>( &scene );
        // ScriptSystem runs BEFORE physics: scripts set the character's move intent (+ look) which
        // PhysicsECSSystem then executes the same frame.
        scene.AddSystem<ECS::ScriptSystem>( &scene, m_AssetManager.get() );
        scene.AddSystem<ECS::PhysicsECSSystem>( &scene );
        // Maps character movement state (speed/onGround from physics) -> locomotion clip. Kept OUT of physics
        // (mechanism vs behaviour); runs after it so it reads this frame's state.
        scene.AddSystem<ECS::LocomotionSystem>( &scene );
        scene.AddSystem<ECS::AudioECSSystem>( &scene );
    }

    Common::BoolResultStr EditorLayer::UpdateSceneFrame( Desert::Core::Scene&    scene,
                                                         Render::RenderRegistry* registry,
                                                         const Common::Timestep& ts )
    {
        // Editor-only selection-outline appearance (from EditorPreferences, not scene data) is pushed per
        // scene before it records this frame.
        if ( auto* sr = scene.GetSceneRenderer() )
        {
            const auto& prefs = EditorPreferences::Get();
            sr->SetOutlineSettings( prefs.OutlineColor, prefs.OutlineWidth, prefs.OutlineSmoothness,
                                    prefs.EnableOutline );
        }

        {
            DESERT_PROFILE_SCOPE( "Scene::BeginScene" );
            if ( auto begin = scene.BeginScene(); !begin )
                return Common::MakeError( begin.GetError() );
        }

        if ( registry )
            registry->Render();

        {
            DESERT_PROFILE_SCOPE( "Scene::OnUpdate" );
            scene.OnUpdate( ts );
        }

        {
            DESERT_PROFILE_SCOPE( "Scene::EndScene" );
            if ( auto end = scene.EndScene(); !end )
                return Common::MakeError( end.GetError() );
        }

        return BOOLSUCCESS;
    }

    void EditorLayer::AddSceneView()
    {
        auto      doc = std::make_unique<SceneDocument>();
        const int idx = static_cast<int>( m_ExtraScenes.size() );
        doc->Name     = "Scene " + std::to_string( idx + 2 ); // the main scene reads as "Scene 1"

        doc->Renderer = std::make_unique<Graphic::SceneRenderer>();
        doc->Scene    = std::make_shared<Desert::Core::Scene>( std::string( doc->Name ), doc->Renderer.get() );
        BuildSceneSystems( *doc->Scene );
        doc->Scene->Init();
        doc->Registry = std::make_unique<Render::RenderRegistry>( doc->Scene );

        // Unique ImGui id per viewport — two windows sharing an id would merge into a single dockable window.
        const std::string title = doc->Name + "###sceneview" + std::to_string( idx );
        auto              vp = std::make_unique<Editor::ViewportPanel>( doc->Scene, m_AssetManager.get(), title );
        vp->SetOnActivate( [this, idx] { SetActiveScene( idx ); } );
        vp->GetVisibility() = true;
        doc->Viewport       = vp.get();
        m_Panels.emplace_back( std::move( vp ) );

        m_ExtraScenes.emplace_back( std::move( doc ) );
        LOG_INFO( "[Editor] Opened a new scene view (now {} scenes open)", m_ExtraScenes.size() + 1 );
    }

    void EditorLayer::SetActiveScene( int index )
    {
        if ( index == m_ActiveSceneIndex || index >= static_cast<int>( m_ExtraScenes.size() ) )
            return;

        m_ActiveSceneIndex = index;
        m_MainScene        = ( index < 0 ) ? m_PrimaryScene : m_ExtraScenes[index]->Scene;

        // Structural undo/redo context + the scene-bound editing panels follow the active document, so the
        // Outliner / Details / Settings / Particle editor all show whichever viewport you are working in.
        Commands::SetContext( m_MainScene.get(), m_AssetManager.get() );
        for ( auto& panel : m_Panels )
            panel->SetScene( m_MainScene );

        // Selection is per-scene (entity UUIDs belong to one registry) — don't carry a stale one across.
        Core::SelectionManager::ClearSelection();

        LOG_INFO( "[Editor] Active scene -> '{}'", m_MainScene->GetSceneName() );
    }

    Common::BoolResultStr EditorLayer::OnImGuiRender()
    {
#ifdef EBABLE_IMGUI
        m_ImGuiLayer->Begin();
#endif

        // ImGuizmo is a single global per-frame state — begin it ONCE here, before any panel issues a
        // Manipulate(). The viewport's object gizmo relies on this.
        ImGuizmo::BeginFrame();

        // ---- Startup loading overlay (UI loader) ----
        // Fullscreen dim + progress while the staged boot work (mesh cook / preload) runs in OnUpdate.
        if ( StartupLoading() )
        {
            ++m_StartupFramesRendered;

            const ImGuiViewport* vp = ::ImGui::GetMainViewport();
            ::ImGui::SetNextWindowPos( vp->Pos );
            ::ImGui::SetNextWindowSize( vp->Size );
            ::ImGui::SetNextWindowBgAlpha( 0.92f );
            ::ImGui::Begin( "##StartupLoader", nullptr,
                            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoDocking );

            const float  cx    = vp->Size.x * 0.5f;
            const float  cy    = vp->Size.y * 0.5f;
            const float  barW  = 420.0f;
            const size_t total = m_StartupStages.size();
            const float  frac  = total ? (float)m_StartupNext / (float)total : 1.0f;
            const char*  label = m_StartupNext < total ? m_StartupStages[m_StartupNext].Label.c_str() : "";

            ::ImGui::SetCursorPos( ImVec2( cx - barW * 0.5f, cy - 60.0f ) );
            ::ImGui::PushFont( EditorResources::GetBoldFont() );
            ::ImGui::TextUnformatted( "DESERT ENGINE" );
            ::ImGui::PopFont();

            ::ImGui::SetCursorPos( ImVec2( cx - barW * 0.5f, cy - 24.0f ) );
            ::ImGui::TextDisabled( "%s", label );

            ::ImGui::SetCursorPos( ImVec2( cx - barW * 0.5f, cy ) );
            ::ImGui::ProgressBar( frac, ImVec2( barW, 8.0f ), "" );

            ::ImGui::SetCursorPos( ImVec2( cx - barW * 0.5f, cy + 20.0f ) );
            ::ImGui::TextDisabled( "%zu / %zu", m_StartupNext, total );

            ::ImGui::End();

            // Loading frames are ImGui-only: no dockspace, no panels (the viewport panel would touch the
            // not-yet-rendered scene image).
#ifdef EBABLE_IMGUI
            m_ImGuiLayer->End();
#endif
            return BOOLSUCCESS;
        }

        // ---- Global editing shortcuts ----
        // Edit mode only (Play discards its changes on Stop anyway) and never while a text field owns the
        // keyboard. Runs at frame start, before any panel iterates the scene.
        {
            ImGuiIO&   io       = ::ImGui::GetIO();
            const bool editMode = m_MainScene->GetState() == ::Desert::Core::Scene::SceneState::Edit;
            if ( editMode && !io.WantTextInput && io.KeyCtrl )
            {
                if ( ::ImGui::IsKeyPressed( ImGuiKey_Z, false ) )
                {
                    if ( io.KeyShift )
                        CommandHistory::Get().Redo();
                    else
                        CommandHistory::Get().Undo();
                }
                if ( ::ImGui::IsKeyPressed( ImGuiKey_Y, false ) )
                    CommandHistory::Get().Redo();

                if ( ::ImGui::IsKeyPressed( ImGuiKey_D, false ) )
                {
                    if ( Core::SelectionManager::Count() > 0 )
                        if ( auto dups = Commands::DuplicateEntities( Core::SelectionManager::GetSelection() );
                             !dups.empty() )
                            Core::SelectionManager::SetSelection( std::move( dups ) );
                }

                if ( ::ImGui::IsKeyPressed( ImGuiKey_C, false ) && Core::SelectionManager::Count() > 0 )
                    Commands::CopySelectionToClipboard( Core::SelectionManager::GetSelection() );
                if ( ::ImGui::IsKeyPressed( ImGuiKey_V, false ) )
                    if ( auto pasted = Commands::PasteClipboard(); !pasted.empty() )
                        Core::SelectionManager::SetSelection( std::move( pasted ) );

                if ( ::ImGui::IsKeyPressed( ImGuiKey_N, false ) )
                    m_NewSceneRequested = true; // Ctrl+N -> fresh empty scene (deferred, see OnUpdate)

                if ( ::ImGui::IsKeyPressed( ImGuiKey_S, false ) )
                {
                    m_MainScene->Serialize( m_AssetManager.get() );
                    s_SavedRevision = CommandHistory::Get().Revision();
                    LOG_INFO( "[Scene] Saved '{}' (Ctrl+S)", m_MainScene->GetSceneName() );
                    Editor::ToastManager::Push( "Saved '" + m_MainScene->GetSceneName() + "'",
                                                Editor::ToastLevel::Success );
                }
            }

            // Command palette (Ctrl+P) — works in both edit and play modes, and even over a text field
            // so it stays reachable; the palette grabs the keyboard once open.
            if ( io.KeyCtrl && !io.KeyShift && ::ImGui::IsKeyPressed( ImGuiKey_P, false ) )
                m_CommandPalette.Open();
        }

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
            const float statusBarHeight = ::ImGui::GetFrameHeight() + 4.0f;
            ImVec2      dockSize        = ::ImGui::GetContentRegionAvail();
            dockSize.y                  = ( dockSize.y > statusBarHeight ) ? dockSize.y - statusBarHeight : 0.0f;

            ImGuiID dockspace_id = ::ImGui::GetID( "MyDockSpace" );

            // One-time auto-relayout: when the default layout's window IDs change (panel-title icons add a
            // ### suffix, changing every window's ImGui ID), old imgui.ini bindings stop matching and panels
            // scatter. Bump kDockLayoutVersion to force a single clean rebuild for everyone, then persist it.
            constexpr int kDockLayoutVersion = 2; // 2: Mesh Editor removed, contextual tools docked
            if ( EditorPreferences::Get().DockLayoutVersion < kDockLayoutVersion )
            {
                m_ResetDefaultLayout                       = true;
                EditorPreferences::Get().DockLayoutVersion = kDockLayoutVersion;
                EditorPreferences::Save();
            }

            // First run (nothing saved in imgui.ini for this dockspace): lay the panels
            // out into a sensible default instead of leaving them floating in a pile.
            // Checked BEFORE DockSpace() — the call itself creates the node. "Reset to Default
            // Layout" (View -> Layouts) forces the same rebuild on demand.
            const bool buildDefaultLayout =
                 ::ImGui::DockBuilderGetNode( dockspace_id ) == nullptr || m_ResetDefaultLayout;
            m_ResetDefaultLayout = false;

            ::ImGui::DockSpace( dockspace_id, dockSize, dockspace_flags );

            if ( buildDefaultLayout )
            {
                ::ImGui::DockBuilderRemoveNode( dockspace_id );
                ::ImGui::DockBuilderAddNode( dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace );
                ::ImGui::DockBuilderSetNodeSize( dockspace_id, ( dockSize.x > 0 && dockSize.y > 0 )
                                                                    ? dockSize
                                                                    : ::ImGui::GetMainViewport()->Size );

                //  ┌───────────┬──────────────────────┬──────────────┐
                //  │ Scene     │                      │ Details      │
                //  │ Outliner  │   Scene (viewport)   ├──────────────┤
                //  ├───────────┤                      │ SceneSettings│
                //  │Collections├──────────────────────┤ / Profiler   │
                //  │           │ Assets / Logs        │ / Foliage    │
                //  └───────────┴──────────────────────┴──────────────┘
                ImGuiID center = dockspace_id;
                ImGuiID right  = ::ImGui::DockBuilderSplitNode( center, ImGuiDir_Right, 0.20f, nullptr, &center );
                ImGuiID left   = ::ImGui::DockBuilderSplitNode( center, ImGuiDir_Left, 0.22f, nullptr, &center );
                ImGuiID bottom = ::ImGui::DockBuilderSplitNode( center, ImGuiDir_Down, 0.28f, nullptr, &center );
                m_BottomDockId = bottom; // remembered so the drawer can be collapsed/restored later
                ImGuiID leftBottom = ::ImGui::DockBuilderSplitNode( left, ImGuiDir_Down, 0.40f, nullptr, &left );
                ImGuiID rightBottom =
                     ::ImGui::DockBuilderSplitNode( right, ImGuiDir_Down, 0.50f, nullptr, &right );

                // Panels routed through the central Begin carry an icon (a ### suffix), so dock them by the
                // SAME composed title — otherwise the icon-changed ImGui ID wouldn't match this assignment.
                // Non-panel windows (Profiler / Foliage / Shader Code) self-Begin with plain names.
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Scene###scene" ).c_str(), center );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Scene Outliner" ).c_str(), left );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Collections" ).c_str(), leftBottom );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Details" ).c_str(), right );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Scene Settings" ).c_str(), rightBottom );
                ::ImGui::DockBuilderDockWindow( "Profiler", rightBottom );
                ::ImGui::DockBuilderDockWindow( "Foliage##FoliagePanel", rightBottom );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Assets" ).c_str(), bottom );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Logs" ).c_str(), bottom );
                ::ImGui::DockBuilderDockWindow( "Shader Code", bottom );

                // Contextual tools (IPanel::IsContextual) get a home too, so the one that opens itself
                // lands where its work belongs instead of floating over the scene: timelines along the
                // bottom next to Assets/Logs, authoring palettes on the right beside Details.
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Sequencer" ).c_str(), bottom );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Anim Layers" ).c_str(), bottom );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Anim Graph" ).c_str(), center );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Particle Editor" ).c_str(), right );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "UI Editor" ).c_str(), right );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Modeling" ).c_str(), left );

                ::ImGui::DockBuilderFinish( dockspace_id );
            }
        }

        for ( const auto& panel : m_Panels )
        {
            if ( !panel->GetVisibility() )
            {
                continue;
            }

            namespace ImGui = ::ImGui;
            // One padding rule for the whole editor, declared by the panel (the viewport asks for zero).
            ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, panel->GetWindowPadding() );

            // First-ever open: give the panel its preferred size, centered on the main viewport —
            // floating tools no longer pop up as tiny windows in a corner. imgui.ini keeps the
            // user's layout afterwards (FirstUseEver never fights it).
            if ( const ImVec2 defSize = panel->GetDefaultSize(); defSize.x > 0.0f && defSize.y > 0.0f )
            {
                ImGui::SetNextWindowSize( defSize, ImGuiCond_FirstUseEver );
                const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos( center, ImGuiCond_FirstUseEver, ImVec2( 0.5f, 0.5f ) );
            }

            // p_open: the title-bar X closes the panel and stays in sync with the View menu. The display
            // title carries an icon but keeps the ImGui ID == GetName() (see PanelDisplayTitle).
            // A panel that just auto-opened is brought to the front of its dock, otherwise it would
            // appear as a background tab nobody notices.
            if ( !m_FocusPanel.empty() && panel->GetName() == m_FocusPanel )
            {
                ImGui::SetNextWindowFocus();
                m_FocusPanel.clear();
            }

            ImGui::Begin( PanelDisplayTitle( panel->GetName() ).c_str(), &panel->GetVisibility() );
            ImGui::PopStyleVar(); // right after Begin: the window kept it, child windows must not inherit
            {
                DESERT_PROFILE_SCOPE_DYNAMIC( panel->GetName().c_str() );
                panel->OnUIRender();
            }
            ImGui::End();
        }

        DrawProfilerWindow();

        DrawStatusBar();

        DrawCommandPalette();
        DrawRecoveryPopup();
        DrawLayoutSavePopup();

        // Transient bottom-right notifications (save/import/validation). Drawn last so they float on top.
        Editor::ToastManager::Get().Draw();

        ::ImGui::End(); // End dockspace

#ifdef EBABLE_IMGUI
        m_ImGuiLayer->End();
#endif
        return BOOLSUCCESS;
    }

    void EditorLayer::DrawCommandPalette()
    {
        if ( !m_CommandPalette.IsOpen() )
            return;

        std::vector<PaletteCommand> commands;
        commands.reserve( m_Panels.size() + 8 );

        // Panels — jump to / reveal any tool window.
        for ( const auto& panel : m_Panels )
        {
            IPanel*     p    = panel.get();
            std::string name = p->GetName();
            if ( const auto hash = name.find( "##" ); hash != std::string::npos )
                name.erase( hash ); // drop the "###id" ImGui suffix for display
            commands.push_back( { "Panel", "Open " + name, [p]
                                  {
                                      p->GetVisibility() = true;
                                      p->Pinned()        = true; // asked for explicitly: keep it open
                                  } } );
        }

        // Entities — select any object in the open scene.
        if ( m_MainScene )
        {
            for ( const auto& entity : m_MainScene->GetAllEntities() )
            {
                if ( !entity.HasComponent<ECS::UUIDComponent>() )
                    continue;
                const Common::UUID uuid = entity.GetComponent<ECS::UUIDComponent>().UUID;
                std::string        name = entity.HasComponent<ECS::TagComponent>()
                                               ? entity.GetComponent<ECS::TagComponent>().Tag
                                               : std::string( "Entity" );
                commands.push_back(
                     { "Entity", std::move( name ), [uuid] { Core::SelectionManager::SetSelected( uuid ); } } );
            }
        }

        // Actions.
        commands.push_back(
             { "Action", "Save Scene", [this] { m_MainScene->Serialize( m_AssetManager.get() ); } } );
        commands.push_back( { "Action", "Undo", [] { CommandHistory::Get().Undo(); } } );
        commands.push_back( { "Action", "Redo", [] { CommandHistory::Get().Redo(); } } );

        m_CommandPalette.SetCommands( std::move( commands ) );
        m_CommandPalette.Draw();
    }

    void EditorLayer::DrawRecoveryPopup()
    {
        namespace ImGui = ::ImGui;

        if ( !m_ShowRecoveryPrompt )
            return;

        constexpr const char* kId = "Recover unsaved work?##recovery";
        ImGui::OpenPopup( kId );

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos( center, ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );

        if ( ImGui::BeginPopupModal( kId, nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextUnformatted( "The previous session ended unexpectedly." );
            ImGui::Spacing();
            ImGui::Text( "Reopen the latest autosave?\n%s", m_RecoveryAutosave.filename().string().c_str() );
            ImGui::Spacing();
            ImGui::TextDisabled( "It opens as an unsaved scene — Save to keep it." );
            ImGui::Separator();

            if ( ImGui::Button( "Reopen autosave", ImVec2( 150.0f, 0.0f ) ) )
            {
                LoadScene( m_RecoveryAutosave );
                m_ShowRecoveryPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Ignore", ImVec2( 100.0f, 0.0f ) ) )
            {
                m_ShowRecoveryPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void EditorLayer::DrawLayoutSavePopup()
    {
        namespace ImGui = ::ImGui;

        if ( !m_ShowSaveLayoutPopup )
            return;

        constexpr const char* kId = "Save Layout##saveLayout";
        ImGui::OpenPopup( kId );

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos( center, ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );

        if ( ImGui::BeginPopupModal( kId, nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextUnformatted( "Layout name:" );
            ImGui::SetNextItemWidth( 260.0f );
            const bool submit = ImGui::InputText( "##layoutName", m_LayoutNameBuf, sizeof( m_LayoutNameBuf ),
                                                  ImGuiInputTextFlags_EnterReturnsTrue );

            const bool valid = !LayoutManager::Sanitize( m_LayoutNameBuf ).empty();
            ImGui::BeginDisabled( !valid );
            if ( ( ImGui::Button( "Save", ImVec2( 110.0f, 0.0f ) ) || submit ) && valid )
            {
                LayoutManager::Save( m_LayoutNameBuf );
                m_ShowSaveLayoutPopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 110.0f, 0.0f ) ) )
            {
                m_ShowSaveLayoutPopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
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

        // The editor is bound to ONE project per run (all content paths are remapped at startup).
        // Switching projects = relaunching through the Project Hub, so the menu only SHOWS the project.
        ImGui::MenuItem(
             ( std::string( ICON_MDI_PACKAGE_VARIANT " " ) + Editor::ProjectContext::Current().Name ).c_str(),
             nullptr, false, false );
        ImGui::TextDisabled( "  switch projects via the Project Hub" );
        ImGui::Separator();

        if ( ImGui::MenuItem( "Open File" ) )
        {
        }
        ImGui::Separator();

        if ( ImGui::MenuItem( "New Scene", "CTRL+N" ) )
        {
            m_NewSceneRequested = true;
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

    // Case-insensitive matching for the scene filter (ASCII: scene paths on disk are ASCII).
    static std::string Lowercased( const std::string& text )
    {
        std::string out = text;
        std::transform( out.begin(), out.end(), out.begin(),
                        []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
        return out;
    }

    // How a scene is NAMED in the pickers: its path relative to the scenes root ("Levels/Arena.desce"),
    // not the bare filename. With subfolders in play, filenames alone are both ambiguous (two "Test.desce"
    // in different folders read identically) and lose the only structure the user gave their scenes.
    static std::string SceneLabel( const Common::Filepath& path )
    {
        std::error_code   ec;
        const std::string rel =
             std::filesystem::relative( path, Common::Constants::Path::SCENE_PATH, ec ).generic_string();

        // Outside the scenes root (a recent scene from elsewhere): a "../../.." chain says nothing.
        if ( ec || rel.empty() || rel.rfind( "..", 0 ) == 0 )
            return path.filename().string();
        return rel;
    }

    void EditorLayer::PrepareScenePopup()
    {
        m_AvailableScenes.clear();

        const auto scenePath = Common::Constants::Path::SCENE_PATH;

        // RECURSIVE: scenes live in subfolders (Levels/, Autosave/, per-feature folders), and a flat scan
        // of the root simply did not list them — they were unreachable from this menu. The error_code
        // overloads also make a missing scenes directory an empty list instead of a thrown exception.
        std::error_code ec;
        auto            it = std::filesystem::recursive_directory_iterator(
             scenePath, std::filesystem::directory_options::skip_permission_denied, ec );
        const auto end = std::filesystem::recursive_directory_iterator();
        for ( ; !ec && it != end; it.increment( ec ) )
        {
            if ( it->path().extension() != Common::Constants::Extensions::SCENE_EXTENSION )
                continue;

            std::error_code fileEc; // separate: a failed stat must not end the whole walk
            if ( std::filesystem::is_regular_file( it->path(), fileEc ) )
                m_AvailableScenes.push_back( it->path() );
        }

        // Sorted by the label the list shows, which keeps every folder's scenes contiguous (they share the
        // "Folder/" prefix) — that is what the folder headers in the popup rely on.
        std::sort( m_AvailableScenes.begin(), m_AvailableScenes.end(),
                   []( const Common::Filepath& a, const Common::Filepath& b )
                   { return SceneLabel( a ) < SceneLabel( b ); } );

        m_SelectedSceneIndex = -1;
        m_SceneFilter[0]     = '\0';
    }

    void EditorLayer::DrawProjectSection()
    {
        namespace ImGui = ::ImGui;

        ImGui::PushFont( Editor::EditorResources::GetBoldFont() );

        ImGui::SameLine( ImGui::GetCursorPosX() + 40.0f );
        ImGui::Separator();
        ImGui::SameLine();

        // The PROJECT name (from the .deproj), not the working directory ("Editor" told you nothing).
        ImGui::TextUnformatted( Editor::ProjectContext::Current().Name.c_str() );
        Utils::ImGuiUtilities::Tooltip( Editor::ProjectContext::FilePath().c_str() );

        // Build configuration badge — you always want to know which binary you are looking at.
#ifdef DESERT_CONFIG_DEBUG
        constexpr const char* kConfig      = "DEBUG";
        const ImVec4          configColour = ImVec4( 0.95f, 0.65f, 0.25f, 1.0f );
#else
        constexpr const char* kConfig      = "RELEASE";
        const ImVec4          configColour = ImVec4( 0.35f, 0.85f, 0.45f, 1.0f );
#endif
        ImGui::SameLine();
        ImGui::TextColored( configColour, "[%s]", kConfig );

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

    // Collapse/restore the bottom drawer (the dock node holding Assets / Logs / Shader Code).
    //
    // ImGui has no "collapse a dock node" call — a docked window trades its collapse arrow for a tab.
    // So collapsing is done by SIZE: the node is squeezed down to its tab bar and restored to the height
    // it had before. That keeps the tabs on screen, which is the whole point of collapsing rather than
    // closing, and it leaves the user's own resize intact because the height is re-read at collapse time.
    void EditorLayer::DrawBottomDrawerToggle()
    {
        namespace ImGui = ::ImGui;

        // Resolve the drawer node from the Assets window's ACTUAL dock node, not from the id captured while
        // building the default layout: that branch only runs for a fresh layout, so with a restored
        // imgui.ini the id stayed 0 and this control was permanently dead.
        ImGuiDockNode* node = m_BottomDockId ? ImGui::DockBuilderGetNode( m_BottomDockId ) : nullptr;
        if ( !node )
        {
            if ( ImGuiWindow* assets = ImGui::FindWindowByName( PanelDisplayTitle( "Assets" ).c_str() );
                 assets && assets->DockNode )
            {
                node           = assets->DockNode;
                m_BottomDockId = node->ID;
            }
        }
        if ( !node )
        {
            ImGui::TextDisabled( ICON_MDI_CHEVRON_DOWN );
            return;
        }

        // Tab bar height + the node's own padding — what "collapsed" means for this node.
        const float collapsedHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;

        const char* icon = m_BottomCollapsed ? ICON_MDI_CHEVRON_UP : ICON_MDI_CHEVRON_DOWN;
        if ( ImGui::SmallButton( icon ) )
        {
            m_BottomCollapsed = !m_BottomCollapsed;
            if ( m_BottomCollapsed )
            {
                // Remember the CURRENT height, not the default: the user may have dragged the splitter.
                m_BottomHeight = node->Size.y;
                ImGui::DockBuilderSetNodeSize( m_BottomDockId, ImVec2( node->Size.x, collapsedHeight ) );
            }
            else
            {
                const float restore = m_BottomHeight > collapsedHeight
                                           ? m_BottomHeight
                                           : ImGui::GetMainViewport()->Size.y * 0.28f; // the layout default
                ImGui::DockBuilderSetNodeSize( m_BottomDockId, ImVec2( node->Size.x, restore ) );
            }
            ImGui::DockBuilderFinish( m_BottomDockId );
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( m_BottomCollapsed ? "Expand the bottom drawer (Assets / Logs)"
                                                 : "Collapse the bottom drawer (Assets / Logs)" );
    }

    void EditorLayer::DrawStatusBar()
    {
        namespace ImGui  = ::ImGui;
        using SceneState = ::Desert::Core::Scene::SceneState;

        const auto   state     = m_MainScene->GetState();
        const char*  stateText = ( state == SceneState::Play )     ? ICON_MDI_PLAY " Play"
                                 : ( state == SceneState::Paused ) ? ICON_MDI_PAUSE " Paused"
                                                                   : ICON_MDI_PENCIL " Edit";
        const ImVec4 stateColor =
             ( state == SceneState::Edit ) ? ThemeManager::GetIconColor() : ThemeManager::GetSelectedColor();

        ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.086f, 0.086f, 0.086f, 1.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f, 2.0f ) );
        ImGui::BeginChild( "##StatusBar", ImVec2( 0.0f, 0.0f ), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

        // The "Content Drawer" / "Output Log" buttons that used to live here are gone. They duplicated
        // the Assets and Logs panels that are already docked along the bottom — two ways to reach one
        // thing, and the button version could only toggle a panel out of existence. What is left is a
        // single chevron that COLLAPSES that bottom drawer instead: a closed panel has to be
        // rediscovered from a menu, a collapsed one is still right there with its tabs visible.
        DrawBottomDrawerToggle();
        ImGui::SameLine( 0.0f, 12.0f );

        // Cmd: one line of Lua against the live scene, the same engine the Lua Console runs. UE puts a
        // console here for the same reason — a question about the running world should not need a panel.
        ImGui::TextDisabled( ICON_MDI_CONSOLE );
        ImGui::SameLine( 0.0f, 4.0f );
        ImGui::SetNextItemWidth( 220.0f );
        if ( ImGui::InputTextWithHint( "##StatusCmd", "Enter Console Command", m_StatusCmd, sizeof( m_StatusCmd ),
                                       ImGuiInputTextFlags_EnterReturnsTrue ) )
        {
            if ( m_StatusCmd[0] != '\0' )
            {
                Core::PanelRequests::Open( "Lua Console" );
                LuaConsolePanel::Submit( m_StatusCmd );
                m_StatusCmd[0] = '\0';
            }
        }
        ImGui::SameLine( 0.0f, 16.0f );

        // Then: scene state + current selection.
        ImGui::PushStyleColor( ImGuiCol_Text, stateColor );
        ImGui::TextUnformatted( stateText );
        ImGui::PopStyleColor();

        // (The scene name + dirty marker moved UP into the window toolbar breadcrumb.)
        ImGui::SameLine( 0.0f, 16.0f );
        if ( const size_t selCount = Core::SelectionManager::Count(); selCount > 1 )
        {
            ImGui::TextDisabled( ICON_MDI_CURSOR_DEFAULT_OUTLINE " %zu selected", selCount );
        }
        else if ( const auto sel = Core::SelectionManager::GetSelected() )
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

        ImGui::SameLine( 0.0f, 16.0f );
        ImGui::TextDisabled( ICON_MDI_CUBE_OUTLINE " %zu entities", m_MainScene->GetAllEntities().size() );

        // Active snap state: off, or the step of the CURRENT transform tool — answers "why did it
        // jump?" without opening the snap popup.
        ImGui::SameLine( 0.0f, 16.0f );
        {
            using Gz = ::Desert::Editor::Core::GizmoState;
            if ( !Gz::PersistentSnap() )
                ImGui::TextDisabled( ICON_MDI_MAGNET " off" );
            else
                switch ( Gz::Get() )
                {
                    case Gz::Operation::Rotate:
                        ImGui::TextDisabled( ICON_MDI_MAGNET " %.1f\xC2\xB0", Gz::RotateSnapDegrees() );
                        break;
                    case Gz::Operation::Scale:
                        ImGui::TextDisabled( ICON_MDI_MAGNET " x%.2f", Gz::ScaleSnap() );
                        break;
                    default:
                        ImGui::TextDisabled( ICON_MDI_MAGNET " %.2fm", Gz::TranslateSnap() );
                        break;
                }
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Snap (toggle in the viewport toolbar; Ctrl inverts while dragging)" );
        }

        // Right: build configuration + frame rate + frame time.
#ifdef DESERT_CONFIG_DEBUG
        constexpr const char* kBuildConfig = "Debug";
#else
        constexpr const char* kBuildConfig = "Release";
#endif
        const float fps = ImGui::GetIO().Framerate;
        char        stats[160];
        std::snprintf( stats, sizeof( stats ), "%s  %s   " ICON_MDI_SPEEDOMETER " %.0f FPS   %.2f ms",
                       Common::Version::Full(), kBuildConfig, fps, fps > 0.0f ? 1000.0f / fps : 0.0f );
        const bool  dirty  = CommandHistory::Get().Revision() != s_SavedRevision;
        const float starW  = dirty ? ImGui::CalcTextSize( "* " ).x : 0.0f;
        const float statsW = ImGui::CalcTextSize( stats ).x;
        ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - statsW - starW );
        if ( dirty )
        {
            // Amber star next to the version/config block = unsaved scene changes.
            ImGui::TextColored( ThemeManager::GetWarningColor(), "*" );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Unsaved changes (Ctrl+S to save)" );
            ImGui::SameLine( 0.0f, ImGui::CalcTextSize( " " ).x );
        }
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

        // Buttons fill the FULL height of the toolbar (minus the child's vertical padding).
        const float  btnH = ImGui::GetContentRegionAvail().y;
        const ImVec2 btnSize( btnH * 1.4f, btnH );

        // Nothing on the left. Save lives on Ctrl+S and in the File menu, the content browser and the log
        // are drawers in the STATUS bar, and edit modes belong to the viewport's own strip — a second row
        // of the same commands up here was just more chrome between the menu and the picture.
        const float spacing   = ImGui::GetStyle().ItemSpacing.x;
        const float playbackW = btnSize.x * 2.0f + spacing; // Play/Stop + Pause
        ImGui::SetCursorPosX( ImGui::GetWindowContentRegionMax().x - playbackW - 4.0f );
        DrawPlayButton( btnSize );
        ImGui::SameLine();
        DrawPauseButton( btnSize );

        ImGui::EndChild();
        ImGui::PopStyleVar( 3 );
        ImGui::PopStyleColor();
    }

    // The profiler table as text. Used by the panel's button AND by --gpu-profile, because a headless shot
    // draws no ImGui and the panel is the only other way these numbers are readable.
    //
    // The GPU column comes from the backend's timestamp queries, so it is device time, not the CPU's wait
    // for it; the two columns disagreeing is the interesting case rather than a fault.
    void EditorLayer::DumpProfilerToLog()
    {
        auto& prof = ::Common::Profiling::Profiler::Get();

        const double frameMs = prof.LastFrameMs();
        const double fps     = frameMs > 0.0001 ? 1000.0 / frameMs : 0.0;

        const std::string frameTotalScope = ::Common::Profiling::kGpuFrameTotalScope;

        double gpuFrameMs = 0.0;
        double gpuSumMs   = 0.0;
        for ( const auto& s : prof.LastFrame() )
        {
            if ( s.Name == frameTotalScope )
                gpuFrameMs = s.GpuMs;
        }

        LOG_INFO( "[Profiler] ---- per-pass breakdown (averaged over {:.1f} s of frames) ----",
                  prof.AvgWindowSeconds() );
        LOG_INFO( "[Profiler] Frame (wall) {:.3f} ms ({:.0f} FPS), GPU frame {:.3f} ms", frameMs, fps,
                  gpuFrameMs );
        LOG_INFO( "[Profiler] {:<34} {:>10} {:>6} {:>10} {:>10} {:>6}", "scope", "cpu ms", "x", "gpu ms",
                  "gpu self", "x" );

        for ( const auto& s : prof.LastFrame() )
        {
            LOG_INFO( "[Profiler] {:<34} {:>10.3f} {:>6} {:>10.3f} {:>10.3f} {:>6}", s.Name, s.TotalMs, s.Calls,
                      s.GpuMs, s.GpuSelfMs, s.GpuCalls );
            // SELF time is the only summable column — the inclusive one counts a parent's microseconds
            // again in each child. The frame bracket is the denominator, not a pass, so it stays out.
            if ( s.GpuCalls > 0 && s.Name != frameTotalScope )
                gpuSumMs += s.GpuSelfMs;
        }

        LOG_INFO( "[Profiler] GPU self times sum to {:.3f} ms of a {:.3f} ms GPU frame ({:.1f} %); the "
                  "remainder is device work no pass is marked around.",
                  gpuSumMs, gpuFrameMs, gpuFrameMs > 0.0001 ? gpuSumMs / gpuFrameMs * 100.0 : 0.0 );
        LOG_INFO( "[Profiler] ---- end ----" );
    }

    void EditorLayer::DrawProfilerWindow()
    {
        namespace ImGui = ::ImGui;
        auto& prof      = ::Common::Profiling::Profiler::Get();

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
        ImGui::SameLine();
        // GPU timestamps are OFF by default: they cost ~8 % of a debug frame on MoltenVK, and an
        // always-on instrument means every later measurement carries the tax. Turning this on is a
        // deliberate act. See Docs/GPU_TIMESTAMPS.md for the measured price.
        ImGui::BeginDisabled( prof.GetGpuSink() == nullptr );
        ImGui::Checkbox( "GPU", &prof.GpuEnabled() );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Device timestamps around every pass.\n"
                               "Costs about 8%% of the frame it measures, so it is off by default." );
        ImGui::SameLine();
        ImGui::BeginDisabled( !prof.GpuEnabled() );
        ImGui::Checkbox( "per-pass", &prof.GpuPassScopes() );
        ImGui::EndDisabled();
        if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
            ImGui::SetTooltip( "Off: time the whole frame only (two timestamps, near-free).\n"
                               "On: also time every pass, which is what costs." );
        ImGui::EndDisabled();
        if ( prof.GetGpuSink() == nullptr && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
            ImGui::SetTooltip( "This device reports no usable timestamp queries — CPU columns only." );

        ImGui::SetNextItemWidth( 160.0f );
        ImGui::SliderFloat( "Avg window (s)", &prof.AvgWindowSeconds(), 0.1f, 2.0f, "%.1f" );

        // The whole-frame GPU bracket the backend records around the command buffer. It is the denominator
        // the per-pass GPU column is checked against: the passes should tile it, not exceed it.
        double gpuFrameMs = 0.0;
        for ( const auto& s : prof.LastFrame() )
            if ( s.Name == ::Common::Profiling::kGpuFrameTotalScope )
                gpuFrameMs = s.GpuMs;

        ImGui::Text( "Frame: %.3f ms  (%.0f FPS)   [avg]", frameMs, fps );
        if ( gpuFrameMs > 0.0 )
        {
            ImGui::SameLine();
            ImGui::TextColored( ImVec4( 0.55f, 0.80f, 1.0f, 1.0f ), "GPU: %.3f ms", gpuFrameMs );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Dump to Log" ) )
            DumpProfilerToLog();

        ImGui::Separator();

        if ( ImGui::BeginTable( "##prof", 6,
                                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                     ImGuiTableFlags_SizingStretchProp ) )
        {
            // The numeric columns are FIXED width and the name stretches. With six columns sharing the
            // width proportionally, the panel docked at its usual size truncated every header to
            // "cp... gp... gpu..." — unreadable, and the two GPU columns are the ones a reader has to
            // tell apart. A millisecond figure needs a known number of characters, not a share of the
            // panel, so it gets one.
            const float kNumWidth = ImGui::CalcTextSize( "0000.000" ).x;
            ImGui::TableSetupColumn( "scope", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "cpu", ImGuiTableColumnFlags_WidthFixed, kNumWidth );
            ImGui::TableSetupColumn( "gpu", ImGuiTableColumnFlags_WidthFixed, kNumWidth );
            ImGui::TableSetupColumn( "self", ImGuiTableColumnFlags_WidthFixed, kNumWidth );
            ImGui::TableSetupColumn( "%", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize( "000" ).x );
            ImGui::TableSetupColumn( "x", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize( "000" ).x );
            ImGui::TableHeadersRow();

            for ( const auto& s : prof.LastFrame() )
            {
                const double pct = frameMs > 0.0001 ? ( s.TotalMs / frameMs ) * 100.0 : 0.0;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( s.Name.c_str() );
                // Docked at its usual width the name column clips, and "Clouds: Sha" / "Clouds: Exe" are
                // two different passes. The full name on hover costs nothing and settles it.
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "%s", s.Name.c_str() );
                ImGui::TableNextColumn();
                ImGui::Text( "%.3f", s.TotalMs );
                ImGui::TableNextColumn();
                // A dash, not 0.000: a scope that records no GPU work and a scope the GPU timer could not
                // reach are different states, and printing zero for both invents a measurement.
                if ( s.GpuCalls > 0 )
                    ImGui::TextColored( ImVec4( 0.55f, 0.80f, 1.0f, 1.0f ), "%.3f", s.GpuMs );
                else
                    ImGui::TextDisabled( "-" );
                ImGui::TableNextColumn();
                // Nested passes subtracted — the column that can be added up.
                if ( s.GpuCalls > 0 )
                    ImGui::TextColored( ImVec4( 0.45f, 0.70f, 0.95f, 1.0f ), "%.3f", s.GpuSelfMs );
                else
                    ImGui::TextDisabled( "-" );
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
        DrawConfirmOpenScenePopup();
        DrawSaveScenePopup();
        DrawNewScenePopup();
        DrawReloadScenePopup();
        DrawProjectPopup();
        DrawPreferencesWindow();
    }

    void EditorLayer::DrawEditMenu()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMenu( "Edit" ) )
        {
            return;
        }

        const bool editMode     = m_MainScene->GetState() == ::Desert::Core::Scene::SceneState::Edit;
        const bool hasSelection = Core::SelectionManager::Count() > 0;

        if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, editMode ) )
            CommandHistory::Get().Undo();
        if ( ImGui::MenuItem( "Redo", "Ctrl+Shift+Z", false, editMode ) )
            CommandHistory::Get().Redo();

        ImGui::Separator();

        if ( ImGui::MenuItem( "Copy", "Ctrl+C", false, editMode && hasSelection ) )
            Commands::CopySelectionToClipboard( Core::SelectionManager::GetSelection() );
        if ( ImGui::MenuItem( "Paste", "Ctrl+V", false, editMode && Commands::ClipboardHasContent() ) )
        {
            if ( auto pasted = Commands::PasteClipboard(); !pasted.empty() )
                Core::SelectionManager::SetSelection( std::move( pasted ) );
        }
        if ( ImGui::MenuItem( "Duplicate", "Ctrl+D", false, editMode && hasSelection ) )
        {
            if ( auto dups = Commands::DuplicateEntities( Core::SelectionManager::GetSelection() ); !dups.empty() )
                Core::SelectionManager::SetSelection( std::move( dups ) );
        }
        if ( ImGui::MenuItem( "Delete", "Del", false, editMode && hasSelection ) )
            Commands::DeleteEntities( Core::SelectionManager::GetSelection() );

        ImGui::Separator();
        if ( ImGui::MenuItem( "Preferences..." ) )
            s_ShowPreferences = true;

        ImGui::EndMenu();
    }

    void EditorLayer::DrawPreferencesWindow()
    {
        namespace ImGui = ::ImGui;
        if ( !s_ShowPreferences )
            return;

        ImGui::SetNextWindowSize( ImVec2( 380.0f, 0.0f ), ImGuiCond_Appearing );
        if ( ImGui::Begin( "Preferences", &s_ShowPreferences, ImGuiWindowFlags_NoDocking ) )
        {
            auto& prefs = EditorPreferences::Get();

            ImGui::Spacing();
            ImGui::TextDisabled( "Editor Camera" );
            ImGui::Separator();
            if ( ImGui::SliderFloat( "Speed", &prefs.CameraSpeed, 0.1f, 10.0f, "%.2fx" ) )
                if ( auto cam = m_MainScene->GetMainCamera().lock() )
                    if ( auto* editorCam = dynamic_cast<::Desert::Core::EditorCamera*>( cam.get() ) )
                        editorCam->SetMovementSpeed( prefs.CameraSpeed );

            ImGui::Spacing();
            ImGui::TextDisabled( "Gizmo Snap" );
            ImGui::Separator();
            bool snapChanged = false;
            snapChanged |= ImGui::Checkbox( "Snap always on (Ctrl inverts)", &prefs.PersistentSnap );
            snapChanged |= ImGui::DragFloat( "Move (m)", &prefs.TranslateSnap, 0.05f, 0.01f, 100.0f, "%.2f" );
            snapChanged |= ImGui::DragFloat( "Rotate (deg)", &prefs.RotateSnapDeg, 0.5f, 0.1f, 180.0f, "%.1f" );
            snapChanged |= ImGui::DragFloat( "Scale", &prefs.ScaleSnap, 0.01f, 0.01f, 10.0f, "%.2f" );
            if ( snapChanged )
            {
                Core::GizmoState::SetTranslateSnap( prefs.TranslateSnap );
                Core::GizmoState::SetRotateSnapDegrees( prefs.RotateSnapDeg );
                Core::GizmoState::SetScaleSnap( prefs.ScaleSnap );
                Core::GizmoState::SetPersistentSnap( prefs.PersistentSnap );
            }

            ImGui::Spacing();
            ImGui::TextDisabled( "Autosave" );
            ImGui::Separator();
            ImGui::SliderInt( "Interval (min)", &prefs.AutosaveMinutes, 0, 30,
                              prefs.AutosaveMinutes == 0 ? "Off" : "%d min" );
            ImGui::TextDisabled( "Autosaves land in Scene/Autosave/, the main file is never touched." );

            ImGui::Spacing();
            ImGui::TextDisabled( "Selection Outline" );
            ImGui::Separator();
            ImGui::Checkbox( "Enable Outline", &prefs.EnableOutline );
            ImGui::ColorEdit3( "Color", glm::value_ptr( prefs.OutlineColor ) );
            ImGui::SliderFloat( "Width (px)", &prefs.OutlineWidth, 0.0f, 20.0f );
            ImGui::SliderFloat( "Smoothness", &prefs.OutlineSmoothness, 0.0f, 10.0f );
            ImGui::TextDisabled( "Live: applied to the selection outline every frame." );

            ImGui::Spacing();
            if ( ImGui::Button( "Save", ImVec2( 110.0f, 0.0f ) ) )
            {
                EditorPreferences::Save();
                s_ShowPreferences = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled( "(persisted to ~/.desertengine/editor.json)" );
        }
        ImGui::End();
    }

    void EditorLayer::DrawViewMenu()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMenu( "View" ) )
        {
            return;
        }

        // Two groups: the panels that are always yours to arrange, and the tools that come and go with
        // the selection. Without the split the menu is twenty entries with no hint that half of them
        // manage themselves — and ticking one of those means "keep it open even when it doesn't apply".
        auto panelItem = [&]( const std::unique_ptr<Editor::IPanel>& panel )
        {
            // Same icon + stable ID as the panel title (the ###id keeps each menu entry unique/stable).
            const bool wasVisible = panel->GetVisibility();
            if ( ImGui::MenuItem( PanelDisplayTitle( panel->GetName() ).c_str(), "", &panel->GetVisibility(),
                                  true ) )
            {
                // Ticking a contextual panel pins it open; unticking releases it back to the context.
                if ( panel->IsContextual() )
                    panel->Pinned() = !wasVisible;
            }
        };

        for ( auto& panel : m_Panels )
            if ( !panel->IsContextual() )
                panelItem( panel );

        ImGui::Separator();
        ImGui::TextDisabled( "Tools (open with the selection)" );
        for ( auto& panel : m_Panels )
            if ( panel->IsContextual() )
                panelItem( panel );

        ImGui::Separator();
        ImGui::MenuItem( "Profiler", "", &m_ShowProfiler, true );
        if ( ImGui::MenuItem( "Perf HUD", "", &EditorPreferences::Get().ShowPerfHud, true ) )
            EditorPreferences::Save(); // persist the toggle like the rest of the user prefs

        ImGui::Separator();
        if ( ImGui::BeginMenu( "Layouts" ) )
        {
            for ( const auto& name : LayoutManager::List() )
            {
                if ( ImGui::MenuItem( name.c_str() ) )
                    LayoutManager::Load( name );
                if ( ImGui::IsItemHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
                    LayoutManager::Delete( name ); // right-click removes it
            }
            ImGui::Separator();
            if ( ImGui::MenuItem( "Save Current Layout..." ) )
            {
                m_LayoutNameBuf[0]    = '\0';
                m_ShowSaveLayoutPopup = true;
            }
            if ( ImGui::MenuItem( "Reset to Default Layout" ) )
                m_ResetDefaultLayout = true;
            ImGui::EndMenu();
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Named docking layouts. Right-click a layout to delete it." );

        ImGui::EndMenu();
    }

    void EditorLayer::SaveSceneTo( const std::string& path )
    {
        std::error_code ec;
        std::filesystem::create_directories( std::filesystem::path( path ).parent_path(), ec );
        Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
        Common::Utils::FileSystem::WriteContentToFile( path, serializer.SerializeToJson() );
    }

    void EditorLayer::BuildStarterScene()
    {
        // A fresh project's first scene = a TEST PLAYGROUND: procedural sky + sun, a ground slab,
        // the classic PBR calibration rows (dielectric + metal, roughness 0..1), glass, an emissive
        // bloom probe, a shadow-caster cluster, coloured fill lights and a playable camera. Only
        // primitives + REAL material assets (created by name in the project's Materials/), so a new
        // project has zero external dependencies and every render feature has something to show on.
        auto prim = [&]( const std::string& name, Geometry::PrimitiveType type, glm::vec3 pos, glm::vec3 scale,
                         Assets::AssetHandle material = Common::UUID::Null() )
        {
            auto& e       = m_MainScene->CreateNewEntity( std::string( name ) );
            auto& smc     = e.AddComponent<ECS::StaticMeshComponent>();
            smc.Primitive = type;
            if ( material )
                smc.MaterialSlots.push_back( material );
            auto& tf       = e.GetComponent<ECS::TransformComponent>();
            // Demo scenes are authored in METRES for readability; a world unit is a centimetre, so every
            // position scales up. Scale does NOT: the primitive meshes themselves are one metre now.
            tf.Translation = pos * Common::Units::UnitsPerMetre;
            tf.Scale       = scale;
        };
        auto mat = [&]( const std::string& name, std::initializer_list<std::pair<const char*, glm::vec4>> params )
        { return Editor::MaterialAssetUtils::CreatePBRMaterialAsset( m_AssetManager.get(), name, params ); };

        // Sun (Translation encodes the direction the light TRAVELS; the sky uses -normalize(T)) + sky.
        // This is the site that MINTED the upside-down sun the shipped Sandbox/Starter scenes carried:
        // normalize(-0.35, -0.9, -0.25) reproduces their corrected value [-0.3509, -0.9023, -0.2506]
        // exactly, so a scene rebuilt from here now matches the one on disk instead of contradicting it.
        auto& sun = m_MainScene->CreateNewEntity( "Sun" );
        sun.AddComponent<ECS::DirectionLightComponent>();
        sun.GetComponent<ECS::TransformComponent>().Translation =
             glm::normalize( glm::vec3( -0.35f, -0.9f, -0.25f ) );

        auto& sky = m_MainScene->CreateNewEntity( "Sky" );
        sky.AddComponent<ECS::SkyAtmosphereComponent>();

        prim( "Ground", Geometry::PrimitiveType::Cube, { 0.0f, -0.1f, 0.0f }, { 24.0f, 0.2f, 24.0f },
              mat( "Starter_Ground", { { "AlbedoColor", { 0.55f, 0.55f, 0.58f, 1.0f } },
                                       { "RoughnessFactor", { 0.9f, 0, 0, 0 } } } ) );

        // PBR calibration rows: roughness 0 -> 1 in 6 steps; front row dielectric, back row metal.
        for ( int i = 0; i < 6; ++i )
        {
            const float roughness = static_cast<float>( i ) / 5.0f;
            const float x         = static_cast<float>( i ) * 1.4f - 3.5f;
            const auto  suffix    = std::to_string( i * 20 );

            prim( "PBR_Dielectric_" + suffix, Geometry::PrimitiveType::Sphere, { x, 0.6f, -3.0f },
                  glm::vec3( 0.55f ),
                  mat( "PBR_D_R" + suffix, { { "AlbedoColor", { 0.85f, 0.20f, 0.15f, 1.0f } },
                                             { "RoughnessFactor", { roughness, 0, 0, 0 } },
                                             { "MetallicFactor", { 0.0f, 0, 0, 0 } } } ) );
            prim( "PBR_Metal_" + suffix, Geometry::PrimitiveType::Sphere, { x, 0.6f, -4.6f }, glm::vec3( 0.55f ),
                  mat( "PBR_M_R" + suffix, { { "AlbedoColor", { 0.95f, 0.93f, 0.88f, 1.0f } },
                                             { "RoughnessFactor", { roughness, 0, 0, 0 } },
                                             { "MetallicFactor", { 1.0f, 0, 0, 0 } } } ) );
        }

        // Glass probe (refraction path) + emissive probe (bloom path — glows past the threshold).
        prim( "GlassSphere", Geometry::PrimitiveType::Sphere, { -2.5f, 1.0f, 0.5f }, glm::vec3( 1.2f ),
              mat( "Starter_Glass", { { "Transmission", { 0.9f, 0, 0, 0 } },
                                      { "IOR", { 1.5f, 0, 0, 0 } },
                                      { "GlassTint", { 0.8f, 0.95f, 1.0f, 1.0f } } } ) );
        prim( "EmissiveCube", Geometry::PrimitiveType::Cube, { 2.5f, 0.5f, 0.5f }, glm::vec3( 1.0f ),
              mat( "Starter_Emissive", { { "AlbedoColor", { 0.1f, 0.1f, 0.1f, 1.0f } },
                                         { "EmissiveColor", { 0.2f, 0.8f, 1.0f, 1.0f } },
                                         { "EmissiveIntensity", { 6.0f, 0, 0, 0 } } } ) );

        // Shadow-caster cluster (different silhouettes for the cascades to chew on).
        const auto clusterMat = mat( "Starter_Prop", { { "AlbedoColor", { 0.80f, 0.45f, 0.20f, 1.0f } },
                                                       { "RoughnessFactor", { 0.6f, 0, 0, 0 } } } );
        prim( "Cube", Geometry::PrimitiveType::Cube, { 0.0f, 0.5f, 1.5f }, glm::vec3( 1.0f ), clusterMat );
        prim( "Cylinder", Geometry::PrimitiveType::Cylinder, { 1.2f, 0.75f, 2.6f }, { 0.6f, 1.5f, 0.6f },
              clusterMat );
        prim( "Capsule", Geometry::PrimitiveType::Capsule, { -1.2f, 0.75f, 2.6f }, { 0.6f, 1.5f, 0.6f },
              clusterMat );

        // Coloured fills (shadowless accents) framing the set.
        auto pointLight = [&]( const char* name, glm::vec3 pos, glm::vec3 color, float intensity )
        {
            auto& e     = m_MainScene->CreateNewEntity( std::string( name ) );
            auto& d     = e.AddComponent<ECS::PointLightComponent>().Data;
            d.Color     = color;
            d.Intensity = intensity;
            d.Radius                                              = Common::Units::Metres( 12.0f );
            e.GetComponent<ECS::TransformComponent>().Translation = pos * Common::Units::UnitsPerMetre;
        };
        pointLight( "FillWarm", { 4.0f, 3.0f, 3.0f }, { 1.0f, 0.85f, 0.6f }, 5.0f );
        pointLight( "FillCool", { -4.0f, 2.5f, -1.0f }, { 0.4f, 0.6f, 1.0f }, 4.0f );

        // SDF text probe: emissive so it blooms like any emissive surface (no special path).
        {
            auto& label          = m_MainScene->CreateNewEntity( "Text" );
            auto& tc             = label.AddComponent<ECS::TextComponent>();
            tc.Text              = "Desert Engine";
            tc.Color             = { 0.55f, 0.85f, 1.0f, 1.0f };
            tc.Size              = Common::Units::Metres( 0.8f );
            tc.EmissiveIntensity = 2.5f; // past the bloom threshold -> the title glows
            auto& ttf            = label.GetComponent<ECS::TransformComponent>();
            ttf.Translation      = Common::Units::Metres( 1.0f ) * glm::vec3( -2.2f, 3.4f, -3.0f );
        }

        auto& camera = m_MainScene->CreateNewEntity( "Camera" );
        camera.AddComponent<ECS::CameraComponent>();
        camera.GetComponent<ECS::TransformComponent>().Translation =
             Common::Units::Metres( 1.0f ) * glm::vec3( 0.0f, 2.5f, 7.0f );
    }

    void EditorLayer::BuildCornellShowcase()
    {
        // Cornell-Box GI + glass showcase. Red/green walls bleed onto the white objects (SSGI); a
        // clear glass sphere sits in front of an orange cube (visible THROUGH it); a point light
        // backlights the set. Colours live in REAL material assets in the mesh slots.
        auto tinted =
             [&]( const char* name, glm::vec3 pos, glm::vec3 scale, const char* matName, glm::vec4 albedo )
        {
            auto& e       = m_MainScene->CreateNewEntity( std::string( name ) );
            auto& smc     = e.AddComponent<ECS::StaticMeshComponent>();
            smc.Primitive = Geometry::PrimitiveType::Cube;
            smc.MaterialSlots.push_back( Editor::MaterialAssetUtils::CreatePBRMaterialAsset(
                 m_AssetManager.get(), matName, albedo, 0.9f ) );
            auto& tf       = e.GetComponent<ECS::TransformComponent>();
            tf.Translation = pos * Common::Units::UnitsPerMetre; // authored in metres (see BuildStarterScene)
            tf.Scale       = scale;
        };
        const glm::vec4 white( 0.82f, 0.82f, 0.80f, 1 ), red( 0.85f, 0.10f, 0.10f, 1 ),
             green( 0.10f, 0.70f, 0.15f, 1 );
        tinted( "CB_Floor", { 0, 0, 0 }, { 6, 0.2f, 6 }, "CB_White", white );
        tinted( "CB_Back", { 0, 3, -3 }, { 6, 6, 0.2f }, "CB_White", white );
        tinted( "CB_LeftRed", { -3, 3, 0 }, { 0.2f, 6, 6 }, "CB_Red", red );
        tinted( "CB_RightGreen", { 3, 3, 0 }, { 0.2f, 6, 6 }, "CB_Green", green );
        // Orange opaque cube directly behind the glass sphere (seen through it).
        tinted( "CB_OrangeCube", { 0, 1.3f, -1.2f }, { 1.4f, 1.4f, 1.4f }, "CB_Orange",
                glm::vec4( 0.95f, 0.5f, 0.08f, 1 ) );

        // Clear glass sphere in front of the cube.
        auto& glass    = m_MainScene->CreateNewEntity( std::string( "CB_GlassSphere" ) );
        auto& gsmc     = glass.AddComponent<ECS::StaticMeshComponent>();
        gsmc.Primitive = Geometry::PrimitiveType::Sphere;
        gsmc.MaterialSlots.push_back( Editor::MaterialAssetUtils::CreatePBRMaterialAsset(
             m_AssetManager.get(), "CB_Glass",
             { { "Transmission", glm::vec4( 0.9f, 0.0f, 0.0f, 0.0f ) },
               { "IOR", glm::vec4( 1.5f, 0.0f, 0.0f, 0.0f ) },
               { "GlassTint", glm::vec4( 0.75f, 0.9f, 1.0f, 1 ) } } ) );
        auto& gtf       = glass.GetComponent<ECS::TransformComponent>();
        gtf.Translation = Common::Units::Metres( 1.0f ) * glm::vec3( 0.0f, 1.5f, 0.7f );
        gtf.Scale       = glm::vec3( 1.6f );

        // Point light BEHIND the objects (backlight / rim).
        auto& pl      = m_MainScene->CreateNewEntity( std::string( "CB_BackLight" ) );
        auto& pld     = pl.AddComponent<ECS::PointLightComponent>().Data;
        pld.Color     = glm::vec3( 1.0f, 0.85f, 0.6f );
        pld.Intensity = 8.0f;
        pld.Radius    = Common::Units::Metres( 12.0f );
        pl.GetComponent<ECS::TransformComponent>().Translation =
             Common::Units::Metres( 1.0f ) * glm::vec3( 0.0f, 2.5f, -2.5f );

        // The baked scene must carry its OWN sun — it no longer piggybacks on startup state.
        // (Exactly one: a second directional light would overflow the single-light UB.)
        if ( m_MainScene->GetRegistry().view<ECS::DirectionLightComponent>().size() == 0 )
        {
            auto& sun = m_MainScene->CreateNewEntity( "CB_Sun" );
            sun.AddComponent<ECS::DirectionLightComponent>();
            // Translation is the direction the light TRAVELS, so a sun ABOVE the horizon points DOWN.
            // This site used to author +Y and put its own sun 57.7 degrees underground; the committed
            // CornellDemo scene carries the corrected value and this now reproduces it exactly
            // (normalize(0.6, -1, 0.2) == [0.5071, -0.8452, 0.1690]).
            sun.GetComponent<ECS::TransformComponent>().Translation =
                 glm::normalize( glm::vec3( 0.6f, -1.0f, 0.2f ) );
        }
    }

    void EditorLayer::LoadScene( const Common::Filepath& path )
    {
        m_SceneLoadRequested = path;
    }

    void EditorLayer::NewSceneInternal()
    {
        // Same teardown as a load, minus the deserialize: clear the current scene to empty and re-init. The
        // Scene object is REUSED (panels hold its shared_ptr), so their references stay valid.
        EngineContext::GetInstance().GetDevice()->WaitIdle();

        CommandHistory::Get().Clear();
        s_SavedRevision = CommandHistory::Get().Revision();

        Core::SelectionManager::ClearSelection();
        m_MainScene->Clear();
        m_MainScene->SetSceneName( "New Scene" );
        m_MainScene->Init();

        // Rebuild the render registry against the fresh registry (its dtor unregisters editor passes by name).
        m_RenderRegistry.reset();
        m_RenderRegistry = std::make_unique<Render::RenderRegistry>( m_MainScene );

        Editor::ToastManager::Push( "New scene", Editor::ToastLevel::Success );
        LOG_INFO( "[Scene] New empty scene" );
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

        // The undo history refers to entities of the OLD scene — none of it applies anymore.
        CommandHistory::Get().Clear();
        s_SavedRevision = CommandHistory::Get().Revision(); // a freshly loaded scene is "clean"

        m_MainScene->Clear();

        Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
        const std::string             content = Common::Utils::FileSystem::ReadFileContent( path );
        serializer.DeserializeFromJson( content );

        m_MainScene->Init();

        // Destroy the old registry FIRST: its destructor unregisters the editor passes by name, and
        // assignment would run it after the new registry already re-registered them.
        m_RenderRegistry.reset();
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

        ImGui::Separator();
        // Multi-scene editing: open a second, independent scene in its own live viewport (own SceneRenderer)
        // so a UI/main-menu scene and the game scene can be worked on side by side without switching. Focus a
        // viewport to make its scene active — the Outliner/Details/gizmo follow it.
        if ( ImGui::MenuItem( ICON_MDI_PLUS_BOX_MULTIPLE " New Scene View" ) )
            m_AddSceneViewRequested = true; // deferred to OnUpdate (allocates GPU resources) — see there
        if ( !m_ExtraScenes.empty() )
            ImGui::TextDisabled( "%d scene view(s) open + main", static_cast<int>( m_ExtraScenes.size() ) );

        if ( !m_RecentScenes.empty() )
        {
            ImGui::Separator();
            ImGui::TextDisabled( "Recent Scenes" );

            for ( const auto& path : m_RecentScenes )
            {
                const std::string label = SceneLabel( path );
                if ( ImGui::MenuItem( label.c_str() ) )
                {
                    LoadScene( path );
                }
                Utils::ImGuiUtilities::Tooltip( path.string().c_str() );
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
        namespace ImGui   = ::ImGui;
        using SceneState  = ::Desert::Core::Scene::SceneState;
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
            auto& e                                              = scene->CreateNewEntity( std::string( name ) );
            e.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Cube;
            auto& t                                              = e.GetComponent<ECS::TransformComponent>();
            t.Translation                                        = localPos * Common::Units::UnitsPerMetre;
            t.Scale                                              = scale;
            auto& col                                            = e.AddComponent<ECS::ColliderComponent>();
            col.Data.Shape                                       = Physics::ShapeType::Box;
            // The Cube primitive spans one metre, so a box of Scale s reaches 50*s units either way.
            col.Data.HalfExtents                                 = scale * ( Common::Units::UnitsPerMetre * 0.5f );
            e.AddComponent<ECS::RigidBodyComponent>().Data.Type  = Physics::BodyType::Static;
            scene->Attach( parent, e );
        }
    } // namespace

    // Builds a walkable greybox HOUSE (floor-less; sits on the demo ground): 4 walls (front wall has a
    // doorway) + a flat roof, each a static collider so the character walks in through the door and is blocked
    // by walls. All parented under one "House" root (a ready prefab root). 2-unit-cube convention: dims = 2*scale.
    void EditorLayer::BuildHouse( const glm::vec3& origin )
    {
        using namespace ::Desert;

        // By VALUE: creating the wall children below reallocates the entity store; a reference would dangle.
        ECS::Entity house                                         = m_MainScene->CreateNewEntity( "House" );
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
            auto& sun         = m_MainScene->CreateNewEntity( "Sun" );
            auto& dl          = sun.AddComponent<ECS::DirectionLightComponent>();
            dl.Data.Color     = { 1.0f, 0.97f, 0.9f };
            dl.Data.Intensity = 3.0f;
            sun.GetComponent<ECS::TransformComponent>().Translation = { -0.4f, -1.0f, -0.5f }; // direction
        }

        // --- Ground: a flat static box the character stands on (mesh + Box collider + Static body)
        {
            auto& ground                                              = m_MainScene->CreateNewEntity( "Ground" );
            ground.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Cube;
            auto& gt              = ground.GetComponent<ECS::TransformComponent>();
            gt.Translation        = Common::Units::Metres( 1.0f ) * glm::vec3( 0.0f, -0.5f, 0.0f ); // top at y=0
            gt.Scale              = { 20.0f, 0.5f, 20.0f };
            auto& gcol            = ground.AddComponent<ECS::ColliderComponent>();
            gcol.Data.Shape       = Physics::ShapeType::Box;
            // Half-extents of the scaled 1 m cube: 50 units per unit of Scale.
            gcol.Data.HalfExtents = gt.Scale * ( Common::Units::UnitsPerMetre * 0.5f );
            ground.AddComponent<ECS::RigidBodyComponent>().Data.Type = Physics::BodyType::Static;
        }

        // --- A few static obstacle boxes to walk into / around
        for ( int i = 0; i < 3; ++i )
        {
            auto& box = m_MainScene->CreateNewEntity( "Obstacle" + std::to_string( i ) );
            box.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Cube;
            auto& bt                                               = box.GetComponent<ECS::TransformComponent>();
            bt.Translation        = Common::Units::Metres( 1.0f ) * glm::vec3( -4.0f + i * 4.0f, 0.5f, -5.0f );
            auto& bcol            = box.AddComponent<ECS::ColliderComponent>();
            bcol.Data.Shape       = Physics::ShapeType::Box;
            bcol.Data.HalfExtents = glm::vec3( Common::Units::Metres( 0.5f ) );
            box.AddComponent<ECS::RigidBodyComponent>().Data.Type  = Physics::BodyType::Static;
        }

        // --- Player: a Character Controller (the physics capsule). NO RigidBody/Collider — the controller
        // IS the physics. The player entity is left UNSCALED so its children (visual body + camera) don't
        // inherit a non-uniform scale (which would skew/displace a child camera and its gizmo). Starts above
        // the ground so it drops on Play. By VALUE: creating children below can reallocate the entity store.
        ECS::Entity player = m_MainScene->CreateNewEntity( "Player" );
        {
            auto& cc       = player.AddComponent<ECS::CharacterControllerComponent>();
            cc.Data.Radius = Common::Units::Metres( 0.3f );
            cc.Data.Height = Common::Units::Metres( 1.8f );
            // Move/jump/look speeds are the SCRIPT's Properties now (Details ▸ Script), not the controller.
            player.GetComponent<ECS::TransformComponent>().Translation =
                 Common::Units::Metres( 1.0f ) * glm::vec3( 0.0f, 3.0f, 0.0f );
            // Movement + mouse-look are now a Lua SCRIPT (engine only executes the physics it asks for).
            {
                ECS::ScriptSlot slot;
                slot.ScriptPath = ( Common::Constants::Path::SCRIPT_PATH / "player_controller.lua" ).string();
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
            body.GetComponent<ECS::TransformComponent>().Translation =
                 Common::Units::Metres( 1.0f ) * glm::vec3( 0.0f, -0.9f, 0.0f );
            m_MainScene->Attach( player, body );
        }

        // --- Camera: a CHILD of the (unscaled) player. Offset behind+above = 3rd person; move it to ~(0,
        // 0.7, 0) with rotation 0 for 1st person. Follows the player via the hierarchy (WORLD transform).
        {
            auto& cam            = m_MainScene->CreateNewEntity( "PlayerCamera" );
            auto& cd             = cam.AddComponent<ECS::CameraComponent>();
            cd.Data.IsMainCamera = true;
            auto& ct             = cam.GetComponent<ECS::TransformComponent>();
            ct.Translation       = Common::Units::Metres( 1.0f ) * glm::vec3( 0.0f, 1.5f, 7.0f ); // 3rd person
            ct.Rotation          = { glm::radians( -10.0f ), 0.0f, 0.0f }; // look slightly down at the player
            m_MainScene->Attach( player, cam );
        }

        BuildHouse( Common::Units::Metres( 1.0f ) * glm::vec3( 12.0f, 0.0f, 0.0f ) ); // greybox house aside

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
        // Play-time changes are discarded on Stop anyway, and the Stop restore recreates every entity —
        // an undo stack recorded against the authored scene must not fire into either state.
        CommandHistory::Get().Clear();
        m_MainScene->SetState( SceneState::Play );
        m_EditorState = EditorState::Play;
    }

    void EditorLayer::OnSceneStop()
    {
        using SceneState = ::Desert::Core::Scene::SceneState;
        if ( m_MainScene->GetState() == SceneState::Edit || m_PlaySnapshot.empty() )
            return;

        EngineContext::GetInstance().GetDevice()->WaitIdle();
        CommandHistory::Get().Clear(); // anything recorded during Play targets entities about to be rebuilt
        m_MainScene->Clear();

        Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
        serializer.DeserializeFromJson( m_PlaySnapshot );
        m_MainScene->Init();

        // Destroy the old registry FIRST: its destructor unregisters the editor passes by name, and
        // assignment would run it after the new registry already re-registered them.
        m_RenderRegistry.reset();
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

            ImGui::SetNextItemWidth( 450.0f );
            ImGui::InputTextWithHint( "##SceneFilter", ICON_MDI_MAGNIFY " Filter", m_SceneFilter,
                                      sizeof( m_SceneFilter ) );

            ImGui::BeginChild( "SceneList", ImVec2( 450, 300 ), true );

            const std::string filter  = Lowercased( m_SceneFilter );
            bool              loadNow = false; // double-click = pick AND load, in one gesture
            std::string       shownFolder;     // last folder header drawn
            bool              haveFolder = false;
            bool              anyShown   = false;

            for ( int i = 0; i < static_cast<int>( m_AvailableScenes.size() ); ++i )
            {
                const std::string label = SceneLabel( m_AvailableScenes[i] );
                if ( !filter.empty() && Lowercased( label ).find( filter ) == std::string::npos )
                    continue;

                // Split "Folder/Sub/Scene.desce" into its folder header and the scene's own name.
                const size_t      slash  = label.find_last_of( '/' );
                const std::string folder = slash == std::string::npos ? std::string() : label.substr( 0, slash );
                const std::string name   = slash == std::string::npos ? label : label.substr( slash + 1 );

                if ( !haveFolder || folder != shownFolder )
                {
                    if ( anyShown )
                        ImGui::Spacing();
                    if ( folder.empty() )
                        ImGui::TextDisabled( ICON_MDI_FOLDER_HOME " Scenes" );
                    else
                        ImGui::TextDisabled( ICON_MDI_FOLDER " %s", folder.c_str() );
                    shownFolder = folder;
                    haveFolder  = true;
                }

                anyShown = true;

                ImGui::PushID( i ); // two folders may hold the same filename
                ImGui::Indent( 12.0f );
                if ( ImGui::Selectable( name.c_str(), m_SelectedSceneIndex == i,
                                        ImGuiSelectableFlags_AllowDoubleClick ) )
                {
                    m_SelectedSceneIndex = i;
                    if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                        loadNow = true;
                }
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "%s", m_AvailableScenes[i].string().c_str() );
                ImGui::Unindent( 12.0f );
                ImGui::PopID();
            }

            if ( !anyShown )
                ImGui::TextDisabled( m_AvailableScenes.empty() ? "No scenes found" : "No match" );

            ImGui::EndChild();

            ImGui::Separator();

            const bool hasSelection =
                 m_SelectedSceneIndex >= 0 && m_SelectedSceneIndex < static_cast<int>( m_AvailableScenes.size() );

            if ( ImGui::Button( "Load", ImVec2( 120, 0 ) ) || loadNow )
            {
                if ( hasSelection )
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

    // Guard for the scene handed over by a panel (viewport drop / asset double-click): the document is
    // about to be replaced, and unlike the menu path this can be triggered by a slip of the mouse. Only
    // shown when there is something to lose — a clean scene opens straight away.
    void EditorLayer::DrawConfirmOpenScenePopup()
    {
        namespace ImGui = ::ImGui;

        if ( m_ConfirmOpenScenePopup )
        {
            ImGui::OpenPopup( "Open Scene?" );
            m_ConfirmOpenScenePopup = false;
        }

        ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                 ImVec2( 0.5f, 0.5f ) );

        if ( !ImGui::BeginPopupModal( "Open Scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
            return;

        const bool havePending = m_PendingOpenScene.has_value();

        ImGui::TextUnformatted( "The current scene has unsaved changes." );
        ImGui::TextDisabled( "Open %s", havePending ? SceneLabel( *m_PendingOpenScene ).c_str() : "" );
        ImGui::Separator();

        if ( ImGui::Button( "Save and Open", ImVec2( 130, 0 ) ) )
        {
            m_MainScene->Serialize( m_AssetManager.get() );
            s_SavedRevision = CommandHistory::Get().Revision();
            if ( havePending )
                LoadScene( *m_PendingOpenScene );
            m_PendingOpenScene.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if ( ImGui::Button( "Discard", ImVec2( 110, 0 ) ) )
        {
            if ( havePending )
                LoadScene( *m_PendingOpenScene );
            m_PendingOpenScene.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if ( ImGui::Button( "Cancel", ImVec2( 110, 0 ) ) )
        {
            m_PendingOpenScene.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorLayer::DrawSaveScenePopup()
    {
        if ( !m_SaveSceneRequested )
        {
            return;
        }

        m_SaveSceneRequested = false;
        m_MainScene->Serialize( m_AssetManager.get() );
        s_SavedRevision = CommandHistory::Get().Revision();
    }

    void EditorLayer::DrawNewScenePopup()
    {
    }

    void EditorLayer::DrawReloadScenePopup()
    {
    }

    void EditorLayer::DrawProjectPopup()
    {
        // Intentionally empty: the editor never opens/switches projects in-session. All content paths
        // are remapped to the project at startup (--project), so switching would require re-initializing
        // the asset manager, cooked caches and panels — relaunch through the Project Hub instead.
    }

    void EditorLayer::OnEvent( Common::Event& event )
    {
#ifdef EBABLE_IMGUI
        for ( auto& panel : m_Panels )
        {
            if ( event.m_Handled )
                break;
            panel->OnEvent( event );
        }
#endif
    }

    Common::BoolResultStr EditorLayer::OnDetach()
    {
        // Clean shutdown: drop the session lock so the next start doesn't think we crashed.
        CrashRecovery::DisarmSession();

        // The app loop exits right after the last PresentFinalImage, so the GPU is still chewing on that
        // frame's command buffer. Panels own GPU objects — offscreen SceneRenderers (Details preview, asset
        // thumbnails, node-graph preview), framebuffers, descriptor pools — and destroying those while that
        // buffer is in flight is what produced the "can't be called on VkPipeline/VkDescriptorPool ... that
        // is currently in use by VkCommandBuffer" validation errors on quit. Idle first, then tear down.
        Graphic::Renderer::GetInstance().WaitDeviceIdle();

#ifdef EBABLE_IMGUI
        m_Panels.clear();
        m_ImGuiLayer->OnDetach();
        m_ImGuiLayer.reset();
#endif
        return BOOLSUCCESS;
    }

} // namespace Desert::Editor
