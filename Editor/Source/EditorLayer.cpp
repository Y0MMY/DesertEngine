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
#include <Engine/Core/Serialize/SceneFormat.hpp>
#include "Editor/Core/CommandLine.hpp"
#include "Editor/Core/Control/ControlChannelOptions.hpp"
#include "Editor/Core/Control/ControlDispatch.hpp" // resolving a request to a palette entry
#include "Editor/Core/CrashRecovery.hpp"

// The device-lost latch, read in OnDetach: a shutdown caused by a lost GPU must save the user's work
// before it goes, and must not report itself as a clean exit.
#include <Engine/Graphic/DeviceLost.hpp>
#include "Editor/Core/LayoutManager.hpp"
#include "Editor/Core/PanelRequests.hpp"
#include "Editor/Core/SceneOpenRequest.hpp"
#include "Editor/Core/SceneSaveRules.hpp"
#include "Editor/Core/ShotOptions.hpp"
#include "Editor/Core/MaterialAssetUtils.hpp"
#include <Engine/Assets/Prefab/PrefabAsset.hpp>
#include <Common/Utilities/FileSystem.hpp>

// 2. Editor Base & Infrastructure
#include "Editor/Core/EditorResources.hpp"
#include "Editor/Core/ThemeManager.hpp"
#include "Editor/Core/GizmoState.hpp"
#include "Editor/Core/NumberFormat.hpp"
#include "Editor/Core/MeshResolve.hpp"            // the toolbar/status triangle census
#include "Editor/Core/Selection/ViewportMode.hpp" // the editor-mode rail
#include <Engine/Geometry/MeshStats.hpp>
#include "Editor/Core/CommandHistory.hpp"
#include "Editor/Core/Commands/SceneCommands.hpp"
#include "Editor/Core/EditorPreferences.hpp"
#include "Editor/Core/ProjectContext.hpp"

#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp> // reading the PRESENTED frame back (shot.window)
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
#include "Editor/Panels/MaterialEditor/MaterialEditorPanel.hpp"
#include "Editor/Panels/MaterialEditor/MaterialDocumentOpen.hpp"
#include "Editor/Panels/Animation/AnimGraphPanel.hpp"
#include "Editor/Panels/Photogrammetry/PhotogrammetryPanel.hpp"
#include "Editor/Panels/Particles/ParticleEditorPanel.hpp"
#include "Editor/Panels/UI/UIEditorPanel.hpp"
#include "Editor/Panels/AssetReferences/AssetReferencesPanel.hpp"
#include "Editor/Panels/LuaConsole/LuaConsolePanel.hpp"
#include "Editor/Panels/Sequencer/SequencerPanel.hpp"
#include "Editor/Panels/Build/BuildSettingsPanel.hpp"
#include "Editor/Panels/History/HistoryPanel.hpp"
#include "Editor/Panels/Validation/SceneValidationPanel.hpp"
#include "Editor/Panels/Clouds/CloudModellingVolumePanel.hpp"
#include "Editor/Panels/Clouds/CloudDocumentOpen.hpp"
#include "Editor/Panels/Clouds/CloudLayoutPanel.hpp"
#include "Editor/Panels/Clouds/CloudNoiseVolumePanel.hpp"
#include "Editor/Panels/Clouds/CloudTypePanel.hpp"
#include "Editor/Panels/Animation/AnimLayersPanel.hpp"
#include "Editor/Core/ToastManager.hpp"
#include "Editor/Core/SubjectEditorRegistry.hpp"
#include "Editor/Core/SubjectOpenRequest.hpp"

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
#include <span>      // the View menu's groups, declared as data rather than as control flow
#include <cctype>    // std::tolower (scene filter)

namespace Desert::Editor
{
    // THE MENU BAR'S OWN MENUS, named once. Read by DrawMenuBar, which opens whichever one is held, and
    // by BuildPaletteCommands, which offers exactly these as commands. Two readers of one list, so the
    // palette cannot offer a menu the bar does not draw — the shape a hand-copied second list always ends
    // up in.
    static constexpr const char* kMenuBarMenus[] = { "File",   "Edit",     "View", "Window",
                                                     "Scenes", "Graphics", "About" };

    // THE SNAP STEPS A PERSON ACTUALLY USES, named once for the same reason the menus above are. Read by
    // DrawSnapControl, which draws them as the magnet popup's list, and by BuildPaletteCommands, which
    // offers exactly these as commands — so the palette cannot offer a step the toolbar does not, which
    // is the shape a hand-copied second list always ends up in.
    //
    // Translation in CENTIMETRES because 1 world unit IS 1 cm here, so the label and the value are the
    // same number and nothing has to be converted in anyone's head.
    static constexpr float kGridSteps[]  = { 1.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f, 500.0f };
    static constexpr float kAngleSteps[] = { 1.0f, 5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 90.0f };

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
        if ( name == "Model from Photos" )
            return ICON_MDI_CUBE_SCAN;
        // "Anim Graph" and "Particle Editor" were here. They are DOCUMENTS now, and a document's icon
        // comes from its registration rather than from a table keyed on a panel name — this table can
        // only ever match a tool's constant name, and a document is named after the thing it edits.
        // See SubjectEditorRegistry::Registration::Icon.
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
            // Details ("Sequencer", "Anim Layers") asked for this panel BY NAME. It pins it, exactly like
            // ticking it in the View menu — the user asked, so nothing auto-closes it.
            //
            // BY NAME IS ALL A TOOL CAN BE ASKED FOR, and that is why the Anim Graph and the Particle
            // Editor no longer come through here: "show the one Anim Graph window" was the most their
            // Details buttons could say. Those two ask for a SUBJECT now
            // (Core::SubjectOpenRequests::Request), which is a different wire because it carries what to
            // edit — see Editor/Core/SubjectOpenRequest.hpp.
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

    // THE DOCUMENT WELL'S OWN WINDOW (layout option B.1). A permanent occupant of the document dock node,
    // for two reasons that are both structural rather than decorative: an empty dock node is not drawn at
    // all, so without it the reserved area would be invisible for exactly as long as it was empty; and it is
    // the only stable window name in that node, which is how DrawDocumentWell recovers the node's runtime id
    // in a session that did not build the layout.
    static constexpr const char* kDocumentWellWindow =
         ICON_MDI_FILE_DOCUMENT_MULTIPLE_OUTLINE "  Documents###documentwell";

    // WHAT A DOCUMENT NOBODY CLAIMS LOOKS LIKE. The registry has no opinion about an unregistered subject
    // type and must not invent one (SubjectEditorRegistry::Icon takes the fallback as an argument for
    // exactly that reason), so the editor's answer lives here, once, rather than at each of the four places
    // that draw a document's glyph.
    static constexpr const char* kUnknownDocumentIcon = ICON_MDI_FILE_DOCUMENT_OUTLINE;

    // THE ICON SWITCH USED TO BE HERE, keyed on Assets::AssetTypeID, and it is gone rather than extended.
    //
    // It was one of the two hand-written tables a new kind of document had to be entered in — the other
    // being AssetTypeName for the text — neither of which is where the document is registered. That is
    // three edits in three files for one new kind, and the two that are not the registration are the ones
    // that get forgotten: the table's `default:` then quietly gave the new kind the generic page glyph and
    // nothing anywhere said so. The icon is now part of the registration
    // (SubjectEditorRegistry::Registration::Icon), so a kind that exists HAS one, by construction. It also
    // had no answer at all for a component subject, whose facet is not an AssetTypeID.

    // The window title a document is drawn with: its type's icon, its subject's name, and the "###doc<...>"
    // identity DocumentTitle already baked into GetName(). NOT PanelDisplayTitle, which would look the icon
    // up by a name that is an asset's and give every document the same fallback.
    std::string EditorLayer::DocumentDisplayTitle( const ISubjectDocument& document ) const
    {
        return std::string( m_SubjectEditors.Icon( document.Subject(), kUnknownDocumentIcon ) ) + "  " +
               DocumentDisplayName( document.GetName() ) + "###" + document.GetName();
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
        // THIS LINE WAS MISSING FROM THE DAY THE PAINTED LAYOUT SHIPPED, and its absence made the whole
        // feature dead: AssetPreloader::PreloadCloudLayouts existed, scanned Clouds/Layouts and registered
        // every `.dclayout` with the service — and nothing ever called it. Every scene binding a painting
        // logged "referenced but not registered" and rendered its sky procedurally. Order-free, like the
        // hero clouds above: a layout names nothing and is named only by a material.
        m_StartupStages.push_back(
             { "Preloading painted layouts...", [this] { m_AssetPreloader->PreloadCloudLayouts(); } } );

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
            const auto demoPath =
                 Common::Constants::Path::SCENE_PATH /
                 ( "CornellDemo" + std::string( Common::Constants::Extensions::SCENE_EXTENSION ) );
            std::error_code ec;
            if ( !std::filesystem::exists( demoPath, ec ) )
            {
                m_MainScene->Clear();
                BuildCornellShowcase();
                // The success line is inside the branch: this bake exists so the demo can be OPENED
                // later, and announcing a file that is not there sends the next reader looking for a
                // corrupt scene instead of a failed write.
                if ( SaveSceneTo( demoPath.generic_string() ) )
                {
                    LOG_INFO( "[Editor] Baked the Cornell showcase -> {}", demoPath.string() );
                }
                else
                {
                    LOG_ERROR( "[Editor] The Cornell showcase was NOT baked to {} — the sandbox has no "
                               "demo scene to open (see the write failure above).",
                               demoPath.string() );
                }
                m_MainScene->Clear();
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
            //
            // The RULE itself lives in Editor/Core/CommandLine.hpp as a pure function taking the existence
            // as a parameter, so it is asserted by a test rather than only observable by launching the
            // editor at a path that is not there. This call site supplies the filesystem it cannot.
            const auto verdict = ValidateSceneForCapture( shot, std::filesystem::exists( shot.Scene ) );
            if ( !verdict.IsSuccess() )
            {
                LOG_ERROR( "[Shot] {} (looked from '{}')", verdict.GetError(),
                           std::filesystem::current_path().string() );
                // NOT std::exit(). The job system's workers are already running by the time this line is
                // reached, and exit() runs static destructors under them: nine threads threw
                // "recursive_mutex lock failed: Invalid argument" and the process aborted with 134. A
                // status of 134 says "the engine crashed", not "the scene you asked for is missing" — the
                // caller reading it learns the wrong thing. Ask for an ordered close with the real status;
                // Run() then draws no frames and teardown happens exactly as on a normal quit.
                const_cast<Engine::Application*>( m_Application )->Close( 2 );
            }
            else
            {
                LoadScene( Common::Filepath( shot.Scene ) );
            }
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
                    if ( SaveSceneTo( scenePath ) )
                    {
                        LOG_INFO( "[Editor] Generated the Starter scene -> {}", scenePath );
                    }
                    else
                    {
                        // The scene is BUILT and open — only the file is missing. Saying so is the
                        // difference between "your new project opens empty next time" being a mystery
                        // and being a known, fixable write failure the user can still Ctrl+S past.
                        LOG_ERROR( "[Editor] The Starter scene was built but NOT written to {} — this "
                                   "project will open empty next time unless it is saved.",
                                   scenePath );
                        Editor::ToastManager::Push( "The Starter scene could not be written — save it "
                                                    "before closing (see the log)",
                                                    Editor::ToastLevel::Error );
                    }
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
        if ( !CrashRecovery::ArmSession() )
            Editor::ToastManager::Push( "Crash recovery is OFF for this session — the lock file could "
                                        "not be written (see the log)",
                                        Editor::ToastLevel::Error );

        // LoadScene( "Resources/Assets/Scene/HouseDemo.desce" );
    }

    EditorLayer::~EditorLayer() = default;

    [[nodiscard]] Common::BoolResultStr EditorLayer::OnAttach()
    {
        // THE CONTROL CHANNEL, IF ONE WAS ASKED FOR. Before anything else, so a client that started this
        // editor can connect and watch the boot rather than guessing how long to wait for the socket.
        //
        // A REFUSAL ENDS THE RUN. Carrying on unheard would be the worst of both: the client waits for a
        // socket that will never appear, and the editor it was meant to drive sits there being driven by
        // nobody. `--control-socket` is only ever passed by something that intends to connect.
        if ( const auto& channel = Control::ControlChannelOptions::Get(); channel.Requested() )
        {
            if ( const auto listening = m_ControlSocket.Listen( channel.SocketPath ); !listening )
                return Common::MakeFormattedError( "control channel: {}", listening.GetError() );
        }

        // 1. Create ImGui Context first
        ::ImGui::CreateContext();

        // 2. Initialize Editor Resources (Adds fonts to the atlas)
        Editor::EditorResources::Initialize( "Resources/Fonts/materialdesignicons-webfont.ttf" );

#ifdef EBABLE_IMGUI
        // 3. Initialize Engine ImGui Layer (Initializes backend and uploads fonts)
        m_ImGuiLayer = ImGui::ImGuiLayer::Create();
        if ( const auto attached = m_ImGuiLayer->OnAttach(); !attached.IsSuccess() )
            return Common::MakeFormattedError( "ImGui layer failed to attach: {}", attached.GetError() );
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

        // NOT INITIALISED WHEN A SCENE LOAD IS ALREADY QUEUED, and that condition is why the line moved
        // rather than why it is conditional. The constructor above has already called LoadScene() for
        // `--scene` or for the project's default scene, so by the time OnAttach gets here the empty "New
        // Scene" this would build a renderer for is a scene NOBODY WILL EVER SEE: OnUpdate returns early
        // for the whole of the staged startup load and draws no scene frame, and the first thing it does
        // when that finishes is LoadSceneInternal, whose own Init() throws this one away. Measured at
        // ~1.4 s of every Debug start (Г8) — pipelines and framebuffers built, waited on and destroyed.
        //
        // It is NOT a "run Init once" flag: re-running Init() is legal and is how a scene load rebuilds
        // the renderer. Only this first, pre-empted one is skipped, and the deferred-load site in
        // OnUpdate is what guarantees the scene ends up initialised even if the load refuses the file.
        //
        // Propagated rather than reported: OnAttach owns a channel and Application::PushLayer now reads
        // it, and an editor whose main scene never initialised has no viewport to show anything in.
        if ( !m_SceneLoadRequested )
        {
            if ( const auto inited = m_MainScene->Init(); !inited.IsSuccess() )
                return Common::MakeFormattedError( "main scene failed to initialise: {}", inited.GetError() );
        }

#ifdef EBABLE_IMGUI
        // EVERY TOOL ENTERS THROUGH PanelRegistry::Add / Adopt, and that is the whole of the guarantee that
        // the View menu lists tools only: the registry REFUSES an ISubjectDocument at compile time, so a
        // document cannot be here to be listed. See Editor/Core/PanelRegistry.hpp.
        m_Panels.Add<Editor::SceneHierarchyPanel>( m_MainScene, m_AssetManager );
        m_Panels.Add<Editor::ScenePropertiesPanel>( m_MainScene, m_AssetManager, m_AnimationLibrary.get() );
        m_Panels.Add<Editor::ShaderLibraryPanel>();
        {
            auto primaryViewport = std::make_unique<Editor::ViewportPanel>( m_MainScene, m_AssetManager.get() );
            // Focusing the main viewport rebinds the editor back to the primary scene.
            primaryViewport->SetOnActivate( [this] { SetActiveScene( kPrimarySceneViewId ); } );
            m_Panels.Adopt( std::move( primaryViewport ) );
        }
        {
            auto fileExplorer = std::make_unique<Editor::FileExplorerPanel>(
                 Common::Constants::Path::ASSETS_PATH, &m_SubjectEditors, m_AssetManager.get(), m_MainScene );
            m_FileExplorerPanel = fileExplorer.get();
            m_Panels.Adopt( std::move( fileExplorer ) );
        }
        m_Panels.Add<Editor::ModelingPanel>( m_MainScene );
        m_Panels.Add<Editor::SceneSettingsPanel>( m_MainScene );
        m_Panels.Add<Editor::LogsPanel>();
        m_Panels.Add<Editor::CollectionsPanel>( m_AssetManager.get() );
        m_Panels.Add<Editor::HistoryPanel>();
        m_Panels.Add<Editor::SceneValidationPanel>( m_MainScene, m_AssetManager.get() );
        // THE FOUR CLOUD PANELS ARE NOT CONSTRUCTED HERE ANY MORE. They were singletons in this list, each
        // reached from the View menu and bound to whatever file its own combo had last opened; they are now
        // asset DOCUMENTS, built on demand by the registry below. Dropping them from the list is what
        // removes them from the View menu, the command palette and `--open-panel <name>` at once — all three
        // are generic over m_Panels, so there was never a per-panel entry to delete. An asset is opened from
        // the asset, not from a menu (Docs/Clouds/DEV_CONTRACT.md §4).

        // Visual stubs for upcoming tools (hidden by default; toggled via the View menu). No real
        // functionality yet — they exist so the layouts/interactions can be iterated on early.
        m_Panels.Add<Editor::NodeGraphPanel>( m_AssetManager );
        // THE ANIM GRAPH AND THE PARTICLE EDITOR ARE NOT CONSTRUCTED HERE ANY MORE, for the reason the
        // four cloud panels above are not: they edit ONE thing, so they are documents. The difference is
        // what that one thing is — a component on an entity rather than a file — which is what U7 made
        // expressible (Editor/Core/EditorSubject.hpp). Dropping them from this list removes them from the
        // View menu, the command palette and `--open-panel` at once, because all three are generic over
        // m_Panels; they are reached from the component that holds them, in Details.
        m_Panels.Add<Editor::PhotogrammetryPanel>( m_MainScene, m_AssetManager.get() );
        m_Panels.Add<Editor::UIEditorPanel>( m_MainScene );
        m_Panels.Add<Editor::AssetReferencesPanel>( m_MainScene, m_AssetManager );
        m_Panels.Add<Editor::LuaConsolePanel>( m_MainScene.get(), m_AssetManager.get() );
        m_Panels.Add<Editor::SequencerPanel>( m_MainScene, m_AnimationLibrary.get(), m_AssetManager.get() );
        m_Panels.Add<Editor::AnimLayersPanel>( m_MainScene, m_AnimationLibrary.get() );
        m_Panels.Add<Editor::BuildSettingsPanel>();

        // ── WHICH EDITOR OPENS WHICH KIND OF SUBJECT ──────────────────────────────────────────────────
        //
        // Double-clicking a `.demat` in the browser opens ONE window bound to THAT material, and a second
        // material is a second window; the four cloud formats follow the same rule, and so — since U7 — do
        // the two kinds of subject that are not files at all. Adding the next kind is another block here
        // rather than another branch in FileExplorerPanel and another file-static inbox beside it. See
        // Editor/Core/SubjectEditorRegistry.hpp.
        //
        // THE NAME AND THE ICON ARE PART OF THE REGISTRATION. They used to be two hand-written tables in
        // this file keyed on Assets::AssetTypeID, so a new kind meant three edits and only one of them was
        // here; the two that were not are the ones that fell behind.
        using Registration = SubjectEditorRegistry::Registration;

        m_SubjectEditors.Register(
             AssetSubjectType( static_cast<uint32_t>( Assets::AssetTypeID::Material ) ),
             Registration{ "Material", ICON_MDI_PALETTE_SWATCH,
                           [this]( const SubjectId& subject ) -> std::unique_ptr<ISubjectDocument> {
                               return std::make_unique<Editor::MaterialEditorPanel>(
                                    Assets::AssetHandle( subject.Owner ), m_AssetManager );
                           },
                           [this]( const SubjectId& subject )
                           {
                               return m_AssetManager && m_AssetManager->FindMetadataByHandle(
                                                             Assets::AssetHandle( subject.Owner ) ) != nullptr;
                           } } );

        // THE FOUR CLOUD DOCUMENTS. Each takes the raw AssetManager pointer the panels already held, so the
        // move from singleton to document changed the panels' ownership of their subject and nothing about
        // how they reach their assets.
        m_SubjectEditors.Register(
             AssetSubjectType( static_cast<uint32_t>( Assets::AssetTypeID::CloudNoiseVolume ) ),
             Registration{ "CloudNoiseVolume", ICON_MDI_GRID,
                           [this]( const SubjectId& subject ) -> std::unique_ptr<ISubjectDocument>
                           {
                               return std::make_unique<Editor::CloudNoiseVolumePanel>(
                                    Assets::AssetHandle( subject.Owner ), m_AssetManager.get() );
                           },
                           [this]( const SubjectId& subject )
                           {
                               return m_AssetManager && m_AssetManager->FindMetadataByHandle(
                                                             Assets::AssetHandle( subject.Owner ) ) != nullptr;
                           } } );
        m_SubjectEditors.Register(
             AssetSubjectType( static_cast<uint32_t>( Assets::AssetTypeID::CloudType ) ),
             Registration{ "CloudType", ICON_MDI_WEATHER_CLOUDY,
                           [this]( const SubjectId& subject ) -> std::unique_ptr<ISubjectDocument> {
                               return std::make_unique<Editor::CloudTypePanel>(
                                    Assets::AssetHandle( subject.Owner ), m_AssetManager.get() );
                           },
                           [this]( const SubjectId& subject )
                           {
                               return m_AssetManager && m_AssetManager->FindMetadataByHandle(
                                                             Assets::AssetHandle( subject.Owner ) ) != nullptr;
                           } } );
        m_SubjectEditors.Register(
             AssetSubjectType( static_cast<uint32_t>( Assets::AssetTypeID::CloudModellingVolume ) ),
             Registration{ "CloudModellingVolume", ICON_MDI_CUBE_OUTLINE,
                           [this]( const SubjectId& subject ) -> std::unique_ptr<ISubjectDocument>
                           {
                               return std::make_unique<Editor::CloudModellingVolumePanel>(
                                    Assets::AssetHandle( subject.Owner ), m_AssetManager.get() );
                           },
                           [this]( const SubjectId& subject )
                           {
                               return m_AssetManager && m_AssetManager->FindMetadataByHandle(
                                                             Assets::AssetHandle( subject.Owner ) ) != nullptr;
                           } } );
        // The layout document also READS the active scene's cloud layer for its preview numbers — the scene
        // is an input, never a second subject, and SetScene keeps it following the focused viewport exactly
        // as the singleton did.
        m_SubjectEditors.Register(
             AssetSubjectType( static_cast<uint32_t>( Assets::AssetTypeID::CloudLayout ) ),
             Registration{ "CloudLayout", ICON_MDI_IMAGE_FILTER_HDR,
                           [this]( const SubjectId& subject ) -> std::unique_ptr<ISubjectDocument>
                           {
                               return std::make_unique<Editor::CloudLayoutPanel>(
                                    Assets::AssetHandle( subject.Owner ), m_MainScene, m_AssetManager.get() );
                           },
                           [this]( const SubjectId& subject )
                           {
                               return m_AssetManager && m_AssetManager->FindMetadataByHandle(
                                                             Assets::AssetHandle( subject.Owner ) ) != nullptr;
                           } } );

        // ── THE TWO DOCUMENTS WHOSE SUBJECT IS NOT A FILE ─────────────────────────────────────────────
        //
        // This is what U7 bought. Both were singleton panels that drew "whatever entity is selected", and
        // both are now opened FROM the component that holds their data, by a button in Details — which is
        // the thing the owner asked for and the thing the old asset-keyed seam could not express.
        //
        // THE FACTORY TAKES THE SCENE THAT IS ACTIVE AT THE MOMENT OF THE OPEN, and that is deliberate:
        // the subject is an entity UUID, and a UUID belongs to ONE registry. Captured by reference to the
        // member so a document opened from the second scene view binds to the second scene — and then
        // never follows the fanout again (AnimGraphPanel::SetScene is a documented no-op).
        //
        // The DISPLAY name is the entity's, resolved once here rather than by the document: the document
        // must not need a scene to know what it is called, and a name is a label while the subject is the
        // identity — renaming the entity does not open a second window.
        m_SubjectEditors.Register(
             Editor::AnimGraphPanel::SubjectType(),
             Registration{ Editor::AnimGraphPanel::kComponentTypeName, ICON_MDI_STATE_MACHINE,
                           [this]( const SubjectId& subject ) -> std::unique_ptr<ISubjectDocument>
                           {
                               return std::make_unique<Editor::AnimGraphPanel>(
                                    subject, SubjectEntityName( subject, "Anim Graph" ), m_MainScene,
                                    m_AnimationLibrary.get() );
                           },
                           [this]( const SubjectId& subject )
                           { return EntityHasComponent<ECS::AnimationComponent>( subject.Owner ); } } );
        m_SubjectEditors.Register(
             Editor::ParticleEditorPanel::SubjectType(),
             Registration{ Editor::ParticleEditorPanel::kComponentTypeName, ICON_MDI_CREATION,
                           [this]( const SubjectId& subject ) -> std::unique_ptr<ISubjectDocument>
                           {
                               return std::make_unique<Editor::ParticleEditorPanel>(
                                    subject, SubjectEntityName( subject, "Particles" ), m_MainScene );
                           },
                           [this]( const SubjectId& subject )
                           { return EntityHasComponent<ECS::ParticleEmitterComponent>( subject.Owner ); } } );

        // ── AND HOW A PATH BECOMES ONE OF THEM ────────────────────────────────────────────────────────
        //
        // The asset browser's double-click used to carry a chain of `else if` over the file types, one arm
        // per kind of document, and this file carried a second copy of the same chain. Registered here
        // instead, beside the editors they feed, so the browser asks once and a new format is a line in
        // this block rather than an edit in two files somebody has to remember exist.
        m_SubjectEditors.RegisterPathOpener(
             [this]( const std::string& path )
             {
                 switch ( RequestMaterialDocument( m_AssetManager.get(), path ) )
                 {
                     case MaterialDocumentRequest::NotAMaterialPath:
                         return SubjectEditorRegistry::PathOpenOutcome::NotMine;
                     case MaterialDocumentRequest::Failed:
                         return SubjectEditorRegistry::PathOpenOutcome::Failed;
                     case MaterialDocumentRequest::Requested:
                         return SubjectEditorRegistry::PathOpenOutcome::Requested;
                 }
                 return SubjectEditorRegistry::PathOpenOutcome::NotMine;
             } );
        m_SubjectEditors.RegisterPathOpener(
             [this]( const std::string& path )
             {
                 switch ( RequestCloudDocument( m_AssetManager.get(), path ) )
                 {
                     case CloudDocumentRequest::NotACloudPath:
                         return SubjectEditorRegistry::PathOpenOutcome::NotMine;
                     case CloudDocumentRequest::Failed:
                         return SubjectEditorRegistry::PathOpenOutcome::Failed;
                     case CloudDocumentRequest::Requested:
                         return SubjectEditorRegistry::PathOpenOutcome::Requested;
                 }
                 return SubjectEditorRegistry::PathOpenOutcome::NotMine;
             } );

        // NOTHING OPENS A PANEL AT BOOT ANY MORE, and the absence is the point.
        //
        // `--open-panel <name>` stood here: a flag that put a tool on screen because macOS refuses this
        // machine synthetic input, so there was no other way to photograph one. It could only ever act
        // ONCE, at startup, which is all a flag can do — and every task that needed a different window
        // added another flag beside it.
        //
        // The control channel replaces the whole family. "Panel" / "Open Details" is a command palette
        // entry, so it is reachable by a person with Ctrl+P and by a client at any moment in the session,
        // as many times as it likes. See BuildPaletteCommands and Editor/Core/Control.
#endif // EBABLE_IMGUI

        // Only when the scene above really was initialised. Every editor pass builds its pipeline
        // against `scene->GetTargetFramebuffer()`, which does not exist until SceneRenderer::Init has
        // run — and when a scene load is already queued that Init is deliberately skipped (see the
        // comment beside it). The load recreates this registry after its own Init, which is what the
        // three other call sites of this line are for.
        if ( m_MainScene->IsInitialized() )
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

        // THE CONTROL CHANNEL GOES FIRST, ahead of the startup-loading return below and not after it.
        //
        // A client connects while the editor is still cooking assets — that is the normal case, since it
        // launched the process — and a channel that only started answering once loading finished would
        // look, from the outside, exactly like an editor that had hung. It answers `state` throughout, so
        // the client can watch the boot; anything that changes the picture is held by the quiescence gate
        // until the staged load is done, which is what PendingWork::StartupLoading is for.
        ServiceControlChannel();

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
            SampleFrameQuiescence();
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

            // THE OTHER HALF OF THE SKIPPED Init() IN OnAttach. That skip is safe only because THIS load
            // initialises the scene — and LoadSceneInternal has three early returns (the file is gone,
            // unreadable, or written by an older build) that deliberately leave the editor exactly as it
            // was. "Exactly as it was" used to mean an initialised empty scene; with the first Init()
            // pre-empted it would mean a scene whose renderer has no systems, and the frame below would
            // record against it. Asked of the SCENE rather than inferred from the load's return value,
            // which is void, and rather than tracked in a flag here, which would be a second copy of a
            // fact the scene already holds.
            if ( !m_MainScene->IsInitialized() )
            {
                if ( const auto inited = m_MainScene->Init(); !inited.IsSuccess() )
                    LOG_ERROR( "[EditorLayer] the scene load was refused and the fallback initialise "
                               "failed too: {}",
                               inited.GetError() );
                // The registry follows the Init that built the framebuffers its passes bind to, exactly
                // as it does on the successful path inside LoadSceneInternal.
                m_RenderRegistry.reset();
                m_RenderRegistry = std::make_unique<Render::RenderRegistry>( m_MainScene );
            }
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

        // ...and closing one destroys the same resources, so it is deferred to the same place. It must also
        // run BEFORE the OnPreUpdate loop below and before UpdateSceneFrame: a document whose window the user
        // dismissed last frame would otherwise get one more full scene render, and — until this existed at
        // all — every subsequent frame for the rest of the session, holding one of the six renderer slots.
        CloseDismissedSceneViews();

        // A DOCUMENT WHOSE SUBJECT HAS GONE IS QUEUED FOR CLOSING BEFORE THE QUEUE IS SERVICED, so the
        // window disappears on the same frame the entity or the asset did rather than one later. It runs
        // after CloseDismissedSceneViews above deliberately: closing a scene view is one of the ways a
        // subject dies, and a document over an entity in that scene must see the scene gone, not still
        // going. See CloseDocumentsWhoseSubjectIsGone.
        CloseDocumentsWhoseSubjectIsGone();

        // Documents follow the scene views exactly, and for the same reason: closing one destroys a Scene
        // and a SceneRenderer, neither of which is legal from inside the ImGui pass that noticed the click.
        // Closes run BEFORE opens so a slot handed back this frame is available to whatever the user is
        // opening in it. The hidden-document slot release rides in the same function, behind the same
        // device-idle wait, for the same reason.
        ServiceDocumentCloses();
        ServiceSubjectOpenRequests();

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
                        Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
                        std::string                   name = m_MainScene->GetSceneName();
                        for ( auto& ch : name )
                            if ( ch == ' ' )
                                ch = '_';
                        const auto      dir = Common::Constants::Path::SCENE_PATH / "Autosave";
                        std::error_code ec;
                        std::filesystem::create_directories( dir, ec );
                        const auto path = dir / ( name + "_autosave" +
                                                  std::string( Common::Constants::Extensions::SCENE_EXTENSION ) );
                        const auto written = ec ? Common::MakeFormattedError( "could not create {}: {}",
                                                                              dir.string(), ec.message() )
                                                : Common::Utils::FileSystem::WriteContentToFileAtomic(
                                                       path, serializer.SerializeToJson() );
                        if ( written )
                        {
                            // The revision is marked done ONLY on a write that landed. It used to be
                            // marked before the write, so a failed autosave was never retried: the next
                            // tick saw the same revision, decided nothing had changed, and skipped —
                            // and the log said the autosave had happened. A user going for their
                            // autosave after a crash found an old file or none.
                            s_LastAutosaveRevision = rev;
                            LOG_INFO( "[Autosave] {}", path.string() );
                        }
                        else
                        {
                            LOG_ERROR( "[Autosave] {} was NOT written: {}. The next autosave tick will "
                                       "try this revision again.",
                                       path.string(), written.GetError() );
                        }
                    }
                }
            }
        }

        // Apply any deferred panel state (e.g. viewport resize) before scene rendering.
        // Panels defer GPU-side resize from OnUIRender to here so descriptor set pools are
        // never destroyed while their DS are bound to the recording command buffer.
        for ( auto& panel : m_Panels )
            panel->OnPreUpdate();
        // The documents get the same call, from their own owner. Two loops rather than one is the visible
        // cost of the split, and it is the cost that buys "the View menu cannot list a document": every
        // place that used to iterate one container now names which of the two it means.
        for ( auto& document : m_Documents )
            document->OnPreUpdate();

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

        // WAS ANYTHING STILL OUTSTANDING WHEN THIS FRAME WAS MADE? Sampled HERE, and the position is the
        // whole of its meaning: after every deferred queue above has drained — scene loads, document
        // closes, asset opens, leaving Play — and before a single pixel of this frame is rendered.
        //
        // Sampled rather than asked for later, because by the time the frame has been presented the
        // answer has moved on, and the question the control channel needs answered is about the picture:
        // "did this frame have everything the command asked for in it, or was some of it still queued?"
        // Editor/Core/Control/ControlPipeline.hpp is where that question is judged.
        SampleFrameQuiescence();

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
                {
                    LOG_ERROR( "[Shot] sequence frame {} not written to '{}'", m_ShotFrame, path );
                    m_ShotFailed = true;
                }
            }

            if ( m_ShotFrame >= shot.Frames )
            {
                if ( !shot.Output.empty() && !WriteViewportPng( shot.Output ) )
                {
                    LOG_ERROR( "[Shot] the final frame was not captured to '{}'", shot.Output );
                    m_ShotFailed = true;
                }
                if ( shot.GpuProfile )
                    DumpProfilerToLog();
                // A capture that wrote no PNG must not leave a zero exit status behind: the whole value of
                // an exit code is that a script can trust it, and this one used to say "fine" either way.
                const_cast<Engine::Application*>( m_Application )->Close( m_ShotFailed ? 1 : 0 );
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

    // The one readback. Every picture the editor writes out of the viewport comes through here, so a
    // capture cannot quietly differ from a dump in flip, format or the device-idle wait that makes the
    // readback legal at all.
    Common::BoolResultStr EditorLayer::ReadViewportRGBA8( std::vector<uint8_t>& outPixels, uint32_t& outWidth,
                                                          uint32_t& outHeight )
    {
        if ( !m_MainScene )
            return Common::MakeError<bool>( "no scene to capture" );

        Graphic::Renderer::GetInstance().WaitDeviceIdle();

        auto img = m_MainScene->GetFinalImage();
        if ( !img )
            return Common::MakeError<bool>( "scene has no final image" );

        outPixels = img->ReadPixelsRGBA8();
        outWidth  = img->GetWidth();
        outHeight = img->GetHeight();
        if ( outPixels.size() != static_cast<size_t>( outWidth ) * outHeight * 4 )
            return Common::MakeFormattedError<bool>( "readback is {} bytes, expected {}x{}x4 = {}",
                                                     outPixels.size(), outWidth, outHeight,
                                                     static_cast<size_t>( outWidth ) * outHeight * 4 );
        return BOOLSUCCESS;
    }

    Common::BoolResultStr EditorLayer::WriteProjectThumbnail()
    {
        // The picture belongs to a project, so with no project there is nowhere for it to go. Not an
        // error: the editor can be running a scene that was opened without one.
        const std::string projectDirectory = Desert::Project::ProjectContext::Directory();
        if ( projectDirectory.empty() )
            return BOOLSUCCESS;

        std::vector<uint8_t> px;
        uint32_t             w = 0;
        uint32_t             h = 0;
        if ( const auto read = ReadViewportRGBA8( px, w, h ); !read.IsSuccess() )
            return read;
        if ( w == 0 || h == 0 )
            return Common::MakeError<bool>( "the viewport has no size" );

        // 16:9 window out of the middle of whatever the viewport is, then a box downsample to the
        // fixed output size. Cropping rather than squashing, because a squashed frame is a picture
        // of the wrong world; centre rather than top, because the interesting part of a viewport is
        // where the camera is pointed.
        constexpr uint32_t kOutW = 512;
        constexpr uint32_t kOutH = 288; // 16:9 — the aspect the launcher's grid is built out of

        uint32_t cropW = w;
        uint32_t cropH = ( w * kOutH ) / kOutW;
        if ( cropH > h )
        {
            cropH = h;
            cropW = ( h * kOutW ) / kOutH;
        }
        const uint32_t cropX = ( w - cropW ) / 2;
        const uint32_t cropY = ( h - cropH ) / 2;

        std::vector<uint8_t> out( static_cast<size_t>( kOutW ) * kOutH * 4 );
        for ( uint32_t y = 0; y < kOutH; ++y )
        {
            // Source rows this output row averages over. Integer bounds on both ends so no source
            // pixel is counted twice and none is skipped.
            const uint32_t sy0 = cropY + ( y * cropH ) / kOutH;
            const uint32_t sy1 = std::max( sy0 + 1u, cropY + ( ( y + 1 ) * cropH ) / kOutH );
            for ( uint32_t x = 0; x < kOutW; ++x )
            {
                const uint32_t sx0 = cropX + ( x * cropW ) / kOutW;
                const uint32_t sx1 = std::max( sx0 + 1u, cropX + ( ( x + 1 ) * cropW ) / kOutW );

                uint32_t accum[4] = { 0, 0, 0, 0 };
                uint32_t samples  = 0;
                for ( uint32_t sy = sy0; sy < sy1 && sy < h; ++sy )
                    for ( uint32_t sx = sx0; sx < sx1 && sx < w; ++sx )
                    {
                        const size_t at = ( static_cast<size_t>( sy ) * w + sx ) * 4;
                        for ( int c = 0; c < 4; ++c )
                            accum[c] += px[at + static_cast<size_t>( c )];
                        ++samples;
                    }
                const size_t dst = ( static_cast<size_t>( y ) * kOutW + x ) * 4;
                for ( int c = 0; c < 4; ++c )
                    out[dst + static_cast<size_t>( c )] =
                         static_cast<uint8_t>( samples ? accum[c] / samples : 0u );
            }
        }

        // The name is a CONVENTION shared with the launcher, which looks for exactly this file
        // beside the .deproj. Leading dot so it does not show up as project content.
        const std::string file = ( std::filesystem::path( projectDirectory ) / ".thumbnail.png" ).string();
        stbi_flip_vertically_on_write( 0 );
        if ( stbi_write_png( file.c_str(), static_cast<int>( kOutW ), static_cast<int>( kOutH ), 4, out.data(),
                             static_cast<int>( kOutW ) * 4 ) == 0 )
            return Common::MakeFormattedError<bool>( "could not write {}", file );
        LOG_INFO( "[Project] thumbnail -> {} ({}x{})", file, kOutW, kOutH );
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

        std::vector<uint8_t> px;
        uint32_t             w = 0;
        uint32_t             h = 0;
        if ( const auto read = ReadViewportRGBA8( px, w, h ); !read.IsSuccess() )
        {
            LOG_ERROR( "[Shot] {} ('{}')", read.GetError(), path );
            return false;
        }

        stbi_flip_vertically_on_write( 0 );
        const bool written = stbi_write_png( path.c_str(), w, h, 4, px.data(), w * 4 ) != 0;
        LOG_INFO( "[Shot] {} -> {} ({}x{})", written ? "wrote" : "FAILED to write", path, w, h );
        return written;
    }

    // =============================================================================================
    // THE CONTROL CHANNEL
    //
    // Four functions and one promise. The promise is that a reply leaves only after a frame that already
    // reflects the command it answers — see Editor/Core/Control/ControlPipeline.hpp for why that is not
    // the same as "the next frame", and what it took to make it true rather than usually true.
    //
    // The order around one frame is:
    //   OnUpdate      ServiceControlChannel()   read a request, run it, arm the gate
    //   OnUpdate      ...deferred queues drain, the scene renders...
    //   OnUpdate      SampleFrameQuiescence()   what was still outstanding while this frame was made
    //   OnImGuiRender ...the interface is recorded into the swapchain...
    //   present
    //   OnFramePresented                        judge the frame; take the shot; release the reply
    // =============================================================================================

    void EditorLayer::ServiceControlChannel()
    {
        if ( !m_ControlSocket.IsListening() )
            return;

        // A request whose reply has not gone out yet holds the channel. Reading a second one here would
        // hand the ordering guarantee to whoever wrote the client: two commands in flight cannot both be
        // "the command the next settled frame proves".
        if ( m_ControlInFlight )
            return;

        const std::optional<std::string> line = m_ControlSocket.PollRequestLine();
        if ( !line )
            return;

        const auto parsed = Control::ParseRequest( *line );
        if ( !parsed )
        {
            // Answered immediately: a request that did not parse has no id to echo and nothing to wait
            // for. Silence here would be indistinguishable from an editor that had stopped reading.
            m_ControlSocket.SendResponseLine(
                 Control::FormatResponse( Control::Response::Failure( 0, parsed.GetError() ) ) );
            return;
        }

        const Control::Request request  = parsed.GetValue();
        Control::Response      response = ExecuteControlRequest( request );

        // READS ANSWER NOW; ANYTHING THAT CAN CHANGE THE PICTURE WAITS FOR ONE.
        //
        // `commands`, `properties` and `state` observe and change nothing, so making them wait would buy
        // latency and no guarantee at all. `run`, `set` and the two shots are the ones the promise is
        // about — and a shot does not merely wait for the settled frame, it IS taken on it, which is why
        // its response is finished in OnFramePresented rather than here.
        //
        // `set` is in the list for exactly the reason `run` is: it moves the preview, and a client that
        // set a value and captured immediately would photograph the frame BEFORE it. That failure is the
        // whole subject of the sequence this document exists to prove.
        const bool waitsForAFrame =
             response.Ok() && ( request.Operation == Control::Op::Run || request.Operation == Control::Op::Set ||
                                Control::IsShot( request.Operation ) );

        if ( !waitsForAFrame )
        {
            m_ControlSocket.SendResponseLine( Control::FormatResponse( response ) );
            if ( request.Operation == Control::Op::Quit && response.Ok() )
                m_ControlQuitCode = request.ExitCode;
            return;
        }

        m_ControlInFlight     = request;
        m_ControlPendingReply = std::move( response );
        m_ControlGate.ArmAfterExecution( m_FrameIndex );
    }

    void EditorLayer::SampleFrameQuiescence()
    {
        Control::EditorQuiescence quiescence;
        quiescence.Set( Control::PendingWork::StartupLoading, StartupLoading() );
        quiescence.Set( Control::PendingWork::SceneLoad, m_SceneLoadRequested.has_value() );
        quiescence.Set( Control::PendingWork::NewScene, m_NewSceneRequested );
        quiescence.Set( Control::PendingWork::SceneView, m_AddSceneViewRequested );
        quiescence.Set( Control::PendingWork::SceneStop, m_PendingSceneStop );
        quiescence.Set( Control::PendingWork::DocumentCloses, !m_DocumentsToClose.empty() );
        // The queue is a file-static inbox drained by ServiceSubjectOpenRequests, so "is anything queued"
        // is asked of the queue itself rather than of a copy this layer keeps — a copy would be a second
        // answer, and the two would disagree on exactly the frame an open was handled halfway.
        quiescence.Set( Control::PendingWork::AssetOpens, Core::SubjectOpenRequests::HasPending() );
        quiescence.Set( Control::PendingWork::OpenRefusal, m_OpenRefusalPending );
        m_FrameQuiescence = quiescence;
    }

    void EditorLayer::OnFramePresented()
    {
        ++m_FrameIndex;

        if ( !m_ControlSocket.IsListening() )
            return;

        // A quit waits for its own answer to leave the machine. A client that asked the editor to close
        // and never heard back cannot tell a clean shutdown from a crash, and it is the last thing it
        // will ever hear from this process.
        if ( m_ControlQuitCode && !m_ControlSocket.HasUnsentOutput() )
        {
            const int32_t code = *m_ControlQuitCode;
            m_ControlQuitCode.reset();
            LOG_INFO( "[Control] quit requested; closing with status {}.", code );
            const_cast<Engine::Application*>( m_Application )->Close( code );
            return;
        }

        if ( !m_ControlInFlight || !m_ControlPendingReply )
            return;

        // The frame just presented is judged by the quiescence sampled while it was being BUILT. The gate
        // uses m_FrameIndex - 1 because the counter was advanced above: the frame that has just gone out
        // is the one that was being made when ServiceControlChannel armed the gate.
        const Control::GateVerdict verdict =
             m_ControlGate.ObserveFramePresented( m_FrameIndex - 1, m_FrameQuiescence );

        if ( verdict == Control::GateVerdict::Waiting || verdict == Control::GateVerdict::Idle )
            return;

        Control::Response reply = *m_ControlPendingReply;

        if ( verdict == Control::GateVerdict::TimedOut )
        {
            reply = Control::Response::Failure(
                 m_ControlInFlight->Id,
                 Control::DescribeSettleTimeout( m_FrameQuiescence, m_ControlGate.FramesWaited() ) );
        }
        else if ( Control::IsShot( m_ControlInFlight->Operation ) )
        {
            // THE SHOT IS TAKEN HERE AND NOWHERE ELSE, and that is the whole reason this hook exists.
            // This instant — after present, before the next acquire — is the only one at which the frame
            // a person would be looking at exists as bytes on the device, and it is a frame this gate has
            // just certified as reflecting the command that came before it.
            std::string error;
            const bool  window  = ( m_ControlInFlight->Operation == Control::Op::ShotWindow );
            const bool  written = window ? WriteWindowPng( m_ControlInFlight->Path, error )
                                         : WriteViewportPng( m_ControlInFlight->Path );

            if ( written )
            {
                rfl::Generic::Object payload;
                payload["path"] = rfl::Generic( m_ControlInFlight->Path );
                // Named on the wire so a report cannot quote a viewport capture as a picture of the
                // editor. The two are different subjects and only one of them contains an interface.
                payload["subject"] = rfl::Generic( std::string( window ? "window" : "viewport" ) );
                reply              = Control::Response::Success( m_ControlInFlight->Id, std::move( payload ) );
            }
            else
            {
                reply = Control::Response::Failure(
                     m_ControlInFlight->Id,
                     error.empty() ? "the capture could not be written; the log has the reason." : error );
            }
        }

        m_ControlSocket.SendResponseLine( Control::FormatResponse( reply ) );
        m_ControlInFlight.reset();
        m_ControlPendingReply.reset();
    }

    Control::Response EditorLayer::ExecuteControlRequest( const Control::Request& request )
    {
        switch ( request.Operation )
        {
            case Control::Op::Commands:
            {
                rfl::Generic::Array entries;
                for ( const PaletteCommand& command : BuildPaletteCommands() )
                {
                    rfl::Generic::Object entry;
                    entry["group"] = rfl::Generic( command.Group );
                    entry["label"] = rfl::Generic( command.Label );
                    entries.push_back( rfl::Generic( entry ) );
                }

                rfl::Generic::Object payload;
                payload["commands"] = rfl::Generic( entries );
                return Control::Response::Success( request.Id, std::move( payload ) );
            }

            case Control::Op::Run:
            {
                // ONE dictionary, built once, both resolved against and run out of. Building it twice —
                // once to look the command up and once to run it — would let the two disagree on any
                // frame where something opened or closed in between, and the command that ran would not
                // be the command that was found.
                const std::vector<PaletteCommand> dictionary = BuildPaletteCommands();
                const Control::CommandAddress     wanted{ request.Group, request.Label };
                const Control::Resolution         resolved = Control::ResolveCommand( dictionary, wanted );

                if ( !resolved.Found )
                {
                    return Control::Response::Failure( request.Id,
                                                       Control::DescribeUnknownCommand( wanted, resolved ) );
                }

                dictionary[resolved.Index].Run();
                return Control::Response::Success( request.Id );
            }

            case Control::Op::Properties:
            {
                ISubjectDocument* focused = m_Documents.Find( m_FocusedDocument );
                if ( !focused )
                {
                    return Control::Response::Failure(
                         request.Id,
                         "no document has the focus, so there is nothing whose properties could be listed. "
                         "Open one — 'commands' offers an entry per openable asset under the group 'Open'." );
                }
                return Control::Response::Success(
                     request.Id, Control::PropertiesToJson( DocumentDisplayName( focused->GetName() ),
                                                            focused->EditableProperties() ) );
            }

            case Control::Op::Set:
            {
                // THE FOCUSED DOCUMENT AND NO OTHER. A property named without a document would have to be
                // searched for across every open window, and the first match would win — which is a
                // different document from the one the person or the capture is looking at, on any frame
                // where two materials declare the same parameter. They almost all do.
                ISubjectDocument* focused = m_Documents.Find( m_FocusedDocument );
                if ( !focused )
                {
                    return Control::Response::Failure(
                         request.Id, "no document has the focus, so '" + request.Property +
                                          "' belongs to nothing. Open the document first; 'state' names the "
                                          "one that has the focus." );
                }

                if ( const auto written = focused->SetEditableProperty( request.Property, request.Value );
                     !written )
                {
                    return Control::Response::Failure( request.Id, written.GetError() );
                }
                return Control::Response::Success( request.Id );
            }

            case Control::Op::State:
            {
                if ( const auto valid = Control::ValidateSections( request.Sections ); !valid )
                    return Control::Response::Failure( request.Id, valid.GetError() );

                return Control::Response::Success( request.Id,
                                                   Control::ToJson( TakeEditorSnapshot(), request.Sections ) );
            }

            case Control::Op::ShotWindow:
            case Control::Op::ShotViewport:
                // Nothing happens now. The capture belongs to the settled frame this request is about to
                // wait for, and is taken in OnFramePresented; answering here would be a picture of the
                // frame BEFORE the commands that preceded it had been drawn.
                return Control::Response::Success( request.Id );

            case Control::Op::Quit:
                return Control::Response::Success( request.Id );
        }

        // Unreachable while every Op is handled above, and stated rather than left to fall off the end:
        // an Op added without a case here would otherwise return a default-constructed response, which is
        // a failure with nothing said — the one thing Response is built to make impossible.
        return Control::Response::Failure( request.Id,
                                           "this operation parsed but has no implementation; that is a "
                                           "defect in the control channel." );
    }

    Control::EditorSnapshot EditorLayer::TakeEditorSnapshot() const
    {
        Control::EditorSnapshot snapshot;

        snapshot.SceneName              = m_MainScene ? m_MainScene->GetSceneName() : std::string();
        snapshot.SceneHasUnsavedChanges = CommandHistory::Get().Revision() != s_SavedRevision;
        snapshot.InPlayMode             = ( m_EditorState == EditorState::Play );

        for ( const Common::UUID& uuid : Core::SelectionManager::GetSelection() )
        {
            Control::EntitySnapshot entity;
            entity.Uuid = uuid.ToString();
            if ( m_MainScene )
            {
                for ( const auto& candidate : m_MainScene->GetAllEntities() )
                {
                    if ( !candidate.HasComponent<ECS::UUIDComponent>() )
                        continue;
                    if ( candidate.GetComponent<ECS::UUIDComponent>().UUID != uuid )
                        continue;
                    if ( candidate.HasComponent<ECS::TagComponent>() )
                        entity.Tag = candidate.GetComponent<ECS::TagComponent>().Tag;
                    break;
                }
            }
            snapshot.Selection.push_back( std::move( entity ) );
        }

        // MOST RECENTLY USED ORDER, which is the order the well lists and Ctrl+Tab walks. Reporting the
        // storage order instead would be a second sequence for the same documents, and a client reading
        // it would predict a different answer from Ctrl+Tab than the editor gives.
        for ( const SubjectId& subject : m_Documents.MostRecentOrder() )
        {
            const ISubjectDocument* document = m_Documents.Find( subject );
            if ( !document )
                continue;

            Control::DocumentSnapshot entry;
            entry.Name               = DocumentDisplayName( document->GetName() );
            entry.Type               = m_SubjectEditors.TypeName( subject );
            entry.Subject            = subject.ToString();
            entry.HoldsRendererSlot  = document->HoldsRendererSlot();
            entry.ClaimsRendererSlot = document->ClaimsRendererSlot();
            entry.Focused            = ( subject == m_FocusedDocument );

            // The three states, asked of the document itself. Written out as words here rather than
            // exported as enums, because the wire is read by clients that have none of our headers.
            entry.EditModel =
                 ( document->GetEditModel() == ISubjectDocument::EditModel::Staged ) ? "staged" : "write-through";
            entry.HasUnappliedEdits = document->HasUnappliedEdits();
            switch ( document->GetDiskState() )
            {
                case ISubjectDocument::DiskState::Clean:
                    entry.DiskState = "clean";
                    break;
                case ISubjectDocument::DiskState::Dirty:
                    entry.DiskState = "dirty";
                    break;
                case ISubjectDocument::DiskState::Untracked:
                    // NOT "clean". A document that took no snapshot has no evidence about its file, and a
                    // client that read the two as one would report an unsaved edit as saved.
                    entry.DiskState = "untracked";
                    break;
            }

            snapshot.Documents.push_back( std::move( entry ) );
        }

        for ( const ClosedDocument& closed : m_Documents.RecentlyClosed() )
        {
            Control::ClosedDocumentSnapshot entry;
            entry.Name    = closed.DisplayName;
            entry.Type    = m_SubjectEditors.TypeName( closed.Subject );
            entry.Subject = closed.Subject.ToString();
            snapshot.RecentlyClosed.push_back( std::move( entry ) );
        }

#ifdef EBABLE_IMGUI
        // TOOLS ONLY, and by construction: m_Panels is a PanelRegistry, which cannot hold a document.
        // A client reading this list is reading exactly what the View menu lists.
        for ( const auto& panel : m_Panels )
        {
            Control::PanelSnapshot entry;
            entry.Name = panel->GetName();
            if ( const auto hash = entry.Name.find( "##" ); hash != std::string::npos )
                entry.Name.erase( hash );
            entry.Visible    = panel->GetVisibility();
            entry.Pinned     = panel->Pinned();
            entry.Contextual = panel->IsContextual();
            entry.Relevant   = panel->IsRelevant();
            snapshot.Panels.push_back( std::move( entry ) );
        }
#endif

        snapshot.RendererSlotsLive    = Graphic::SceneRenderer::GetLiveRendererCount();
        snapshot.RendererSlotsPending = PendingRendererSlotDemand( m_Documents.Documents() );
        snapshot.RendererSlotsMax     = EngineContext::kMaxRendererSlots;

        snapshot.LogInfoCount    = LogsPanel::InfoCount();
        snapshot.LogWarningCount = LogsPanel::WarningCount();
        snapshot.LogErrorCount   = LogsPanel::ErrorCount();
        snapshot.LogTail         = LogsPanel::Tail( 40 );

        snapshot.Quiescence = m_FrameQuiescence;
        return snapshot;
    }

    void EditorLayer::RecordWindowCaptureIfDue()
    {
        // THE CAPTURE IS RECORDED WHILE THE FRAME IS STILL BEING BUILT, and that is not an optimisation.
        //
        // A swapchain image may only be touched between its acquire and its present. The first version of
        // this read it back after the present, in OnFramePresented, and the picture was correct — which is
        // exactly what made it dangerous. Only the validation layer objected: "vkQueueSubmit(): performs a
        // layout transition on presentable VkImage, but the image has not been acquired from
        // VkSwapchainKHR". A capture that quietly breaks the frame loop it was taken to document is worth
        // less than no capture.
        //
        // So the editor asks the gate, before the submit, whether THIS frame is the one the reply waits
        // for — the same question, on the same inputs, that OnFramePresented will answer afterwards.
        if ( !m_ControlInFlight || m_ControlInFlight->Operation != Control::Op::ShotWindow )
            return;
        if ( !m_ControlGate.WouldDischarge( m_FrameIndex, m_FrameQuiescence ) )
            return;

        auto swapChain = std::dynamic_pointer_cast<Graphic::API::Vulkan::VulkanSwapChain>(
             EngineContext::GetInstance().GetWindow()->GetWindowSwapChain() );
        if ( !swapChain )
        {
            m_ControlCaptureError = "there is no Vulkan swapchain to capture the presented frame from.";
            return;
        }

        if ( const auto recorded = swapChain->RecordFrameCapture(); !recorded )
        {
            m_ControlCaptureError = recorded.GetError();
            return;
        }
        m_ControlCaptureError.clear();
    }

    bool EditorLayer::WriteWindowPng( const std::string& path, std::string& outError )
    {
        // THE WHOLE EDITOR, INTERFACE INCLUDED — which is what WriteViewportPng next door cannot do and
        // never could. That one reads the scene's own final image; ImGui is recorded into the SWAPCHAIN
        // render pass, so no capture this engine took before this existed held a single pixel of a panel,
        // a menu or a dialog. It is why proving anything about the interface meant photographing the
        // window from outside the process, by PID.
        //
        // This is the second half of the capture: the copy was recorded into this frame's command buffer
        // by RecordWindowCaptureIfDue, and the bytes are collected here, once the present that carried it
        // has gone out.
        if ( !m_ControlCaptureError.empty() )
        {
            outError = m_ControlCaptureError;
            m_ControlCaptureError.clear();
            return false;
        }

        auto swapChain = std::dynamic_pointer_cast<Graphic::API::Vulkan::VulkanSwapChain>(
             EngineContext::GetInstance().GetWindow()->GetWindowSwapChain() );
        if ( !swapChain )
        {
            outError = "there is no Vulkan swapchain to collect the captured frame from.";
            return false;
        }

        const std::filesystem::path file = std::filesystem::path( path );
        if ( file.has_parent_path() && !file.parent_path().empty() )
        {
            std::error_code ec;
            std::filesystem::create_directories( file.parent_path(), ec );
            if ( ec && !std::filesystem::exists( file.parent_path() ) )
            {
                outError = "could not create '" + file.parent_path().string() + "': " + ec.message();
                return false;
            }
        }

        // The copy was submitted with this frame; waiting is what makes the staging buffer readable.
        Graphic::Renderer::GetInstance().WaitDeviceIdle();

        uint32_t   width  = 0;
        uint32_t   height = 0;
        const auto pixels = swapChain->TakeCapturedFrameRGBA8( width, height );
        if ( !pixels )
        {
            outError = pixels.GetError();
            return false;
        }

        stbi_flip_vertically_on_write( 0 );
        const bool written = stbi_write_png( path.c_str(), static_cast<int>( width ), static_cast<int>( height ),
                                             4, pixels.GetValue().data(), static_cast<int>( width ) * 4 ) != 0;
        if ( !written )
        {
            outError = "stb_image_write refused to write '" + path + "'.";
            return false;
        }

        LOG_INFO( "[Control] wrote the presented frame (editor and interface) -> {} ({}x{})", path, width,
                  height );
        return true;
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
        // Editor-only VIEW state (from EditorPreferences, not scene data) pushed per scene before it
        // records this frame: the selection outline, and — since К2 — the debug/show flags that used to be
        // serialized into the level. Both must land BEFORE BeginScene, which is where the renderer hands
        // them on to its systems.
        //
        // Only scenes that reach this function are pushed to, and that is the point: the asset-thumbnail,
        // inspector-preview and photogrammetry renderers own their own SceneRenderer, are never fed here,
        // and therefore keep DebugViewState's all-off defaults. They used to have to remember to switch the
        // grid off by hand on a scene they owned.
        if ( auto* sr = scene.GetSceneRenderer() )
        {
            const auto& prefs = EditorPreferences::Get();
            sr->SetOutlineSettings( prefs.OutlineColor, prefs.OutlineWidth, prefs.OutlineSmoothness,
                                    prefs.EnableOutline );
            sr->SetDebugView( prefs.DebugView );
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
        auto           doc = std::make_unique<SceneDocument>();
        const uint64_t id  = m_SceneViewIds.Next();
        doc->Id            = id;
        // Numbered by the id, not by the current count: with closing implemented, "Scene 3" reappearing as
        // the name of a fourth view after the third was closed would put two different documents under one
        // label across a session, and the log lines below are how a slot leak is read.
        doc->Name = "Scene " + std::to_string( id + 1 ); // the main scene reads as "Scene 1"

        doc->Renderer = std::make_unique<Graphic::SceneRenderer>();
        doc->Scene    = std::make_shared<Desert::Core::Scene>( std::string( doc->Name ), doc->Renderer.get() );
        BuildSceneSystems( *doc->Scene );
        // Reported: AddSceneView is void and the document is already in the well by the time this runs, so
        // there is nothing to hand a failure to. What matters is that the log names the view — a scene that
        // did not initialise renders an empty viewport, which reads as a content problem, not an engine one.
        if ( const auto inited = doc->Scene->Init(); !inited.IsSuccess() )
            LOG_ERROR( "[EditorLayer] scene view '{}' failed to initialise: {}", doc->Name, inited.GetError() );
        doc->Registry = std::make_unique<Render::RenderRegistry>( doc->Scene );

        // Unique ImGui id per viewport — two windows sharing an id would merge into a single dockable window.
        // Keyed on the document id so a closed window's saved imgui.ini entry (position, dock node, size) is
        // never inherited by an unrelated later view.
        const std::string title = doc->Name + "###sceneview" + std::to_string( id );
        auto              vp = std::make_unique<Editor::ViewportPanel>( doc->Scene, m_AssetManager.get(), title );
        // Captures the ID, never the index. See Editor/Core/SceneViewIdentity.hpp.
        vp->SetOnActivate( [this, id] { SetActiveScene( id ); } );
        vp->GetVisibility() = true;
        doc->Viewport       = vp.get();
        m_Panels.Adopt( std::move( vp ) );

        m_ExtraScenes.emplace_back( std::move( doc ) );
        LOG_INFO( "[Editor] Opened scene view #{} (now {} scenes open, {}/{} renderer slots in use)", id,
                  m_ExtraScenes.size() + 1, Graphic::SceneRenderer::GetLiveRendererCount(),
                  EngineContext::kMaxRendererSlots );
    }

    void EditorLayer::CloseDismissedSceneViews()
    {
        // Collect first, close after: CloseSceneView erases from both m_ExtraScenes and m_Panels, so deciding
        // and mutating in one pass over either would be iterating a container while emptying it.
        std::vector<uint64_t> dismissed;
        for ( const auto& doc : m_ExtraScenes )
            if ( doc->Viewport && !doc->Viewport->GetVisibility() )
                dismissed.push_back( doc->Id );

        for ( const uint64_t id : dismissed )
            CloseSceneView( id );
    }

    void EditorLayer::CloseSceneView( uint64_t id )
    {
        const auto index = IndexOfSceneView(
             m_ExtraScenes, []( const std::unique_ptr<SceneDocument>& doc ) { return doc->Id; }, id );
        if ( !index )
            return; // already closed — a second X on the same window in the same frame, or a stale request

        // Closing the document that is PLAYING ends play mode with it. The snapshot Stop would restore is a
        // snapshot of a scene that is about to cease existing, and OnSceneStop acts on whatever document is
        // active — so leaving the state alone would strand the editor in Play with an Edit-mode scene under
        // it: the Stop button would early-return and never come back up.
        if ( m_EditorState == EditorState::Play && m_ActiveSceneId == id )
        {
            LOG_INFO( "[Editor] Scene view #{} was playing when it was closed — play mode ends with it and "
                      "its snapshot is discarded.",
                      id );
            m_EditorState      = EditorState::Paused;
            m_PendingSceneStop = false;
            m_PlaySnapshot.clear();
        }

        // The editor must not stay bound to a scene that is about to stop existing. Rebinding BEFORE the
        // teardown, not after, so no panel is holding the dying scene when its registry is destroyed.
        if ( const uint64_t next = ActiveSceneViewAfterClose( m_ActiveSceneId, id ); next != m_ActiveSceneId )
            SetActiveScene( next );

        auto&             doc  = m_ExtraScenes[*index];
        const std::string name = doc->Name;

        // The destruction order is the one ~PreviewViewport established and it is not interchangeable: the
        // last submitted frame may still be executing against this document's pipelines, framebuffers and
        // descriptor pools. Idle the device; then drop the PANEL (its UIHelper holds descriptor sets that
        // reference the scene's images); then the registry, which holds render commands built from the
        // scene; then the scene, which owns the passes; and only then the renderer that owns them all —
        // whose destructor is what hands the renderer slot back.
        Graphic::Renderer::GetInstance().WaitDeviceIdle();

        IPanel* panel = doc->Viewport;
        m_ContextualShown.erase( panel );
        m_Panels.Remove( panel );
        doc->Viewport = nullptr;

        doc->Registry.reset();
        doc->Scene.reset();
        doc->Renderer.reset();
        m_ExtraScenes.erase( m_ExtraScenes.begin() + static_cast<ptrdiff_t>( *index ) );

        // The count is printed, not left to be derived: a surface that fails to return its slot produces no
        // error at all, and this line beside the one in AddSceneView is what makes the leak readable.
        LOG_INFO( "[Editor] Closed scene view #{} '{}' ({} scenes open, {}/{} renderer slots in use)", id, name,
                  m_ExtraScenes.size() + 1, Graphic::SceneRenderer::GetLiveRendererCount(),
                  EngineContext::kMaxRendererSlots );
    }

    std::vector<EditorLayer::RendererSlotConsumer> EditorLayer::RendererSlotCensus() const
    {
        std::vector<RendererSlotConsumer> census;
        census.push_back( { "main viewport", true } ); // the primary scene's renderer exists for the session

        for ( const auto& doc : m_ExtraScenes )
            census.push_back( { "scene view '" + doc->Name + "'", true } );

        // The Details preview is a TOOL that happens to own a renderer, so it is found among the panels.
        for ( const auto& panel : m_Panels )
            if ( const auto* details = dynamic_cast<const ScenePropertiesPanel*>( panel.get() ) )
                census.push_back( { "Details preview", details->HoldsRendererSlot() } );

        // The documents are asked of their own owner rather than sifted out of the panel list with a
        // dynamic_cast. That cast was the seam an earlier task closed: it only existed because the two
        // kinds shared a container, and every place that had to write it was a place that could forget to.
        for ( const auto& document : m_Documents )
        {
            // The VISIBLE half of the name. The census tells a user what to close, and they close a window
            // titled "MP_GreenTint", not one titled "MP_GreenTint###docasset:2:3333333333333333333".
            census.push_back( { m_SubjectEditors.TypeName( document->Subject() ) + " document '" +
                                     DocumentDisplayName( document->GetName() ) + "'",
                                document->HoldsRendererSlot(), document->ClaimsRendererSlot(),
                                document->Subject() } );
        }

        return census;
    }

    std::string EditorLayer::SubjectEntityName( const SubjectId& subject, const char* what ) const
    {
        // WHAT THE WINDOW IS CALLED, not what it is. The subject is the identity; this is the label beside
        // it, and it is resolved ONCE, here, at the moment the document is built — the document itself must
        // not need a scene to know its own name, and an entity renamed afterwards does not become a second
        // window (an asset document behaves the same way; see Control::DocumentSnapshot::Subject).
        std::string name = "Entity";
        if ( m_MainScene )
        {
            if ( const auto entOpt = m_MainScene->FindEntityByID( subject.Owner ) )
                name = entOpt->get().GetComponent<ECS::TagComponent>().Tag;
        }
        return name + " \xc2\xb7 " + what;
    }

    void EditorLayer::ServiceSubjectOpenRequests()
    {
        for ( const SubjectId& subject : Core::SubjectOpenRequests::Drain() )
        {
            // OPEN-OR-FOCUS, keyed by the subject, asked of the one owner of open documents. It does NOT
            // set a visibility flag any more: a document that is open is open, and "focus" is the only
            // thing a second request for the same subject can mean.
            if ( ISubjectDocument* open = m_Documents.Find( subject ) )
            {
                FocusDocument( open->Subject() );
                continue;
            }

            // Checked BEFORE the slot arithmetic below, so a kind with no editor is reported as the missing
            // editor it is rather than as a resource shortage it had nothing to do with.
            if ( !m_SubjectEditors.HasEditorFor( subject.Type() ) )
            {
                LOG_WARN( "[Editor] Nothing edits subject '{}' — no window opened.", subject.ToString() );
                continue;
            }

            // THE SEVENTH CONSUMER IS REFUSED, OUT LOUD. There are six renderer slots. A document admitted
            // past the cap would not fail — SceneRenderer would hand it slot 0 to share with the main view,
            // and the symptom is two surfaces quietly trading each other's per-frame camera some minutes
            // later, with no error anywhere. So the count is checked here and the census is printed with
            // names, because a bare "no slots" leaves the user with nothing to close.
            //
            // Pending demand is counted separately and it is not pedantry: a document that is open but has
            // not drawn yet holds NO slot and has a claim coming, so the live-renderer count alone would
            // admit a document there is no slot for and discover it a frame later.
            //
            // The counting rule itself lives in SubjectEditorRegistry.hpp, not here: this file is compiled
            // by no suite, and a rule written in it is a rule nothing can assert.
            const uint32_t live    = Graphic::SceneRenderer::GetLiveRendererCount();
            const uint32_t pending = PendingRendererSlotDemand( m_Documents.Documents() );

            if ( live + pending >= EngineContext::kMaxRendererSlots )
            {
                auto        rows = RendererSlotCensus();
                std::string census;
                for ( const auto& consumer : rows )
                {
                    const char* state = consumer.HoldsSlot ? "holds a slot"
                                        : consumer.ClaimsSlot
                                             ? "no slot right now, but will claim one when it draws"
                                             : "holds no slot and never will (drawn on the CPU) — closing "
                                               "it frees nothing";
                    census += "\n    " + consumer.Name + " — " + state;
                }
                LOG_ERROR( "[Editor] Refusing to open a document for subject '{}': {} of {} renderer slots "
                           "are in use and {} more are already committed. Close one of these first:{}",
                           subject.ToString(), live, EngineContext::kMaxRendererSlots, pending, census );

                // AND THE SAME THING WHERE THE USER IS. The census above has always been written; it went
                // to a log the user was not reading, so a double-click on the seventh document did nothing
                // at all as far as the screen was concerned. The dialog carries the identical rows and, for
                // the ones that are documents, a button that acts on them.
                //
                // The subject is named by its FILE NAME or its ENTITY NAME where one is known: "handle
                // 3333333333333333333" is the log's identifier, not the user's.
                m_OpenRefusal = OpenRefusal{ RefusedSubjectName( subject ), m_SubjectEditors.TypeName( subject ),
                                             live, pending, std::move( rows ) };
                m_OpenRefusalPending = true;
                continue;
            }

            auto document = m_SubjectEditors.Create( subject );
            if ( !document )
                continue; // the registry already said why

            const std::string name = document->GetName();
            // Asked BEFORE the move, and counted rather than assumed: a document that will never claim a
            // slot adds nothing to the committed total, and "pending + 1" would have reported every cloud
            // document as a claim on a slot it does not take. See ISubjectDocument::ClaimsRendererSlot.
            const uint32_t committed = pending + ( document->ClaimsRendererSlot() ? 1u : 0u );

            // ASKED THE MOMENT IT IS BUILT, and not left to the sweep a frame later. A document whose
            // subject was already gone would otherwise appear for one frame and vanish, which reads as a
            // window that failed rather than as a thing that is not there. The factory has just resolved
            // the subject, so this costs one more resolution and answers before anything is on screen.
            if ( !document->IsSubjectAlive() )
            {
                LOG_WARN( "[Editor] Refusing to open a document for subject '{}': the {} it names does not "
                          "exist (deleted, or in a scene that is no longer open).",
                          subject.ToString(), m_SubjectEditors.TypeName( subject ) );
                continue;
            }

            m_Documents.Add( std::move( document ) );
            m_FocusPanel      = name; // brings the new window forward in the document well
            m_FocusedDocument = subject;
            LOG_INFO( "[Editor] Opened a '{}' document '{}' ({} open, {}/{} renderer slots in use, {} "
                      "committed).",
                      m_SubjectEditors.TypeName( subject ), name, m_Documents.Count(),
                      Graphic::SceneRenderer::GetLiveRendererCount(), EngineContext::kMaxRendererSlots,
                      committed );
        }
    }

    std::string EditorLayer::RefusedSubjectName( const SubjectId& subject ) const
    {
        if ( subject.Domain == SubjectDomain::Asset && m_AssetManager )
        {
            // The UNTYPED metadata lookup, deliberately: the refusal happens before any editor for this
            // type is consulted, so all that is known about the subject is that it is an asset — and a
            // typed lookup would have to guess which class to ask for. Metadata carries no cast, so there
            // is nothing here that could answer with a stranger.
            if ( const auto* metadata =
                      m_AssetManager->FindMetadataByHandle( Assets::AssetHandle( subject.Owner ) ) )
                return metadata->Filepath.stem().string();
        }
        if ( subject.Domain == SubjectDomain::EntityComponent && m_MainScene )
        {
            if ( const auto entOpt = m_MainScene->FindEntityByID( subject.Owner ) )
                return entOpt->get().GetComponent<ECS::TagComponent>().Tag;
        }
        return "this subject";
    }

    void EditorLayer::RequestDocumentClose( const SubjectId& subject, std::string reason )
    {
        if ( !m_Documents.Find( subject ) )
            return; // already gone, or never open — a second x on one window in one frame is not an error

        const auto queued =
             std::find_if( m_DocumentsToClose.begin(), m_DocumentsToClose.end(),
                           [&subject]( const PendingDocumentClose& p ) { return p.Subject == subject; } );
        if ( queued == m_DocumentsToClose.end() )
            m_DocumentsToClose.push_back( PendingDocumentClose{ subject, std::move( reason ) } );
    }

    void EditorLayer::RequestCloseAllDocuments()
    {
        // Collected first and requested after, rather than requested while iterating: RequestDocumentClose
        // reads the well, and a range-for over a container something else is being asked about is the kind
        // of thing that survives review and then does not survive a refactor.
        std::vector<SubjectId> subjects;
        subjects.reserve( m_Documents.Count() );
        for ( const auto& document : m_Documents )
            subjects.push_back( document->Subject() );

        for ( const SubjectId& subject : subjects )
            RequestDocumentClose( subject, "Close All Documents" );
    }

    void EditorLayer::CloseDocumentsWhoseSubjectIsGone()
    {
        // ── A DOCUMENT CLOSES WITH ITS SUBJECT ────────────────────────────────────────────────────────
        //
        // The owner's decision, and the counterpart to the one he refused: a document is NOT closed when
        // it loses the focus, because a layout that rearranges itself reads as an editor that lost your
        // panel. It IS closed when the thing it edits stops existing — delete the entity, remove the
        // component, delete the asset, close the scene — because the alternative is a window editing
        // nothing, and every one of its controls then writes into a resolution that returns null.
        //
        // WITH A NAMED REASON. Three different causes now queue a close, and a user whose window vanished
        // is owed which one it was; ServiceDocumentCloses prints it.
        //
        // ASKED EVERY FRAME, and it has to be. There are FOUR ways a subject dies and no single event
        // covers them: an asset leaves the manager, an entity is destroyed, a COMPONENT is removed from an
        // entity that survives, or the scene a document was opened over is closed. A subscription to one
        // of the four would be worse than none, because the other three would then look handled. The cost
        // is one resolution per open document per frame — the same resolution each document already
        // performs to draw itself, and there are rarely more than six of them.
        std::vector<SubjectId> dead;
        for ( const auto& document : m_Documents )
            if ( !document->IsSubjectAlive() )
                dead.push_back( document->Subject() );

        for ( const SubjectId& subject : dead )
        {
            RequestDocumentClose( subject, "the " + m_SubjectEditors.TypeName( subject ) +
                                                " it was editing no longer exists" );
        }
    }

    void EditorLayer::ReleaseSlotsOfHiddenDocuments()
    {
        // ── THE SLOT GOES WHEN NOBODY IS LOOKING; THE WINDOW STAYS ────────────────────────────────────
        //
        // Four documents docked as tabs in one node show one tab. The other three were rendering previews
        // nobody could see and holding three of the six renderer slots while they did it, so the fifth
        // document the user opened was refused over resources being spent on hidden windows.
        //
        // DrawDocuments counts the frames each document has gone undrawn (ImGui::Begin answers false for a
        // collapsed window and for an inactive tab); this acts on the count. Called from
        // ServiceDocumentCloses so it runs behind the SAME device-idle wait a close uses — releasing a
        // PreviewViewport destroys a Scene and a SceneRenderer, and the last submitted frame may still be
        // executing against them.
        for ( const auto& document : m_Documents )
        {
            const auto it = m_DocumentHiddenFrames.find( document->Subject() );
            if ( it == m_DocumentHiddenFrames.end() || it->second < kFramesHiddenBeforeSlotRelease )
                continue;
            if ( !document->HoldsRendererSlot() )
                continue;

            document->ReleaseRendererSlot();

            // VERIFIED, NOT ASSUMED. ReleaseRendererSlot's contract is that HoldsRendererSlot answers false
            // afterwards; a document that inherited the empty default while genuinely holding a slot would
            // otherwise keep it for ever and the census would go on blaming a window the user cannot fix.
            if ( document->HoldsRendererSlot() )
            {
                LOG_ERROR( "[Editor] '{}' was asked to release its renderer slot after {} hidden frames and "
                           "still holds one. ReleaseRendererSlot must make HoldsRendererSlot false — see "
                           "ISubjectDocument.",
                           DocumentDisplayName( document->GetName() ), it->second );
                continue;
            }

            LOG_INFO( "[Editor] '{}' gave its renderer slot back after {} frames off screen ({}/{} in use). "
                      "It is rebuilt on the first frame the window is drawn again.",
                      DocumentDisplayName( document->GetName() ), it->second,
                      Graphic::SceneRenderer::GetLiveRendererCount(), EngineContext::kMaxRendererSlots );
        }
    }

    void EditorLayer::ServiceDocumentCloses()
    {
        // The hidden-document sweep shares this function's device-idle wait, so the wait is taken when
        // either has work. Two waits in one frame would be two full pipeline drains for one frame's worth
        // of teardown.
        bool releasePending = false;
        for ( const auto& document : m_Documents )
        {
            const auto it = m_DocumentHiddenFrames.find( document->Subject() );
            if ( it != m_DocumentHiddenFrames.end() && it->second >= kFramesHiddenBeforeSlotRelease &&
                 document->HoldsRendererSlot() )
            {
                releasePending = true;
                break;
            }
        }

        if ( m_DocumentsToClose.empty() && !releasePending )
            return;

        // ONE device-idle wait for the whole batch. Destroying a document destroys its PreviewViewport, and
        // with it the scene, the renderer and the renderer slot; the last submitted frame may still be
        // executing against that renderer's pipelines, framebuffers and descriptor pools. The ordering is
        // the one ~PreviewViewport and CloseSceneView both established, not a precaution invented here.
        Graphic::Renderer::GetInstance().WaitDeviceIdle();

        ReleaseSlotsOfHiddenDocuments();

        for ( const PendingDocumentClose& pending : m_DocumentsToClose )
        {
            // Released, then destroyed HERE. The well hands ownership back rather than dropping the object
            // itself, because it is this function that knows the device is idle — see DocumentWell.
            std::unique_ptr<ISubjectDocument> closed = m_Documents.Release( pending.Subject );
            if ( !closed )
                continue;

            const std::string name = closed->GetName();
            m_ContextualShown.erase( closed.get() );
            m_DocumentHiddenFrames.erase( pending.Subject );
            if ( m_FocusedDocument == pending.Subject )
                m_FocusedDocument = SubjectId{};

            closed.reset();

            // The REASON is printed, and it is why this line takes one. "Closed document 'Hero'" leaves a
            // user who did not close it with nothing to go on; "because the AnimationComponent it was
            // editing no longer exists" is the whole answer.
            //
            // The slot count is printed rather than derived for a different reason: a document that failed
            // to return its slot produces no error at all, and this line beside the one in
            // ServiceSubjectOpenRequests is what makes the leak readable.
            LOG_INFO( "[Editor] Closed document '{}' — {} ({} open, {}/{} renderer slots in use after "
                      "release).",
                      DocumentDisplayName( name ), pending.Reason, m_Documents.Count(),
                      Graphic::SceneRenderer::GetLiveRendererCount(), EngineContext::kMaxRendererSlots );
        }

        m_DocumentsToClose.clear();
    }

    void EditorLayer::FocusDocument( const SubjectId& subject )
    {
        ISubjectDocument* document = m_Documents.Find( subject );
        if ( !document )
            return;

        m_Documents.Touch( subject );
        m_FocusedDocument = subject;
        m_FocusPanel      = document->GetName(); // brings it forward in whatever dock it lives
    }

    void EditorLayer::CycleDocuments()
    {
        const auto next = m_Documents.NextMostRecent( m_FocusedDocument );
        if ( !next )
            return;

        ISubjectDocument* document = m_Documents.Find( *next );
        if ( !document )
            return;

        // Focus WITHOUT touching the ring. Committing the new order on every press would make the second
        // Ctrl+Tab return to where the first started, so the order is committed when Ctrl is released —
        // see m_CyclingDocuments in OnImGuiRender.
        m_FocusedDocument  = *next;
        m_FocusPanel       = document->GetName();
        m_CyclingDocuments = true;
    }

    void EditorLayer::SetActiveScene( uint64_t id )
    {
        if ( id == m_ActiveSceneId )
            return;

        const auto index = IndexOfSceneView(
             m_ExtraScenes, []( const std::unique_ptr<SceneDocument>& doc ) { return doc->Id; }, id );
        if ( id != kPrimarySceneViewId && !index )
        {
            // NAMED rather than ignored. An id that resolves to nothing means a viewport outlived its
            // document, which is a lifetime bug in this file — and the whole reason activation is keyed on an
            // id is that this case can be SEEN. An index would have silently activated a neighbour.
            LOG_ERROR( "[Editor] Scene view #{} asked to become active but no such document is open — the "
                       "active scene is unchanged ('{}').",
                       id, m_MainScene ? m_MainScene->GetSceneName() : "<none>" );
            return;
        }

        m_ActiveSceneId = id;
        m_MainScene     = index ? m_ExtraScenes[*index]->Scene : m_PrimaryScene;

        // Structural undo/redo context + the scene-bound editing panels follow the active document, so the
        // Outliner / Details / Settings / Particle editor all show whichever viewport you are working in.
        Commands::SetContext( m_MainScene.get(), m_AssetManager.get() );
        for ( auto& panel : m_Panels )
            panel->SetScene( m_MainScene );
        // Documents follow the active scene too. The Cloud Layout document READS the focused scene's cloud
        // layer for its preview numbers — the scene is an input, never a second subject — and it stopped
        // following it the moment documents left the panel list, which is exactly the "a middle link drops a
        // property" shape this codebase has paid for seven times.
        for ( auto& document : m_Documents )
            document->SetScene( m_MainScene );

        // Selection is per-scene (entity UUIDs belong to one registry) — don't carry a stale one across.
        Core::SelectionManager::ClearSelection();

        LOG_INFO( "[Editor] Active scene -> '{}' (view #{})", m_MainScene->GetSceneName(), id );
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
                    // Deliberately discarded HERE and only here: Ctrl+S destroys nothing, so there is
                    // no next step to gate. SaveOpenScene has already put the star back on and told the
                    // user why if the write failed.
                    (void)SaveOpenScene();
                }
            }

            // Command palette (Ctrl+P) — works in both edit and play modes, and even over a text field
            // so it stays reachable; the palette grabs the keyboard once open.
            if ( io.KeyCtrl && !io.KeyShift && ::ImGui::IsKeyPressed( ImGuiKey_P, false ) )
                m_CommandPalette.Open();

            // CTRL+TAB THROUGH THE DOCUMENTS, most recently used first. This is what makes ten open
            // documents bearable: past about six the tab you want is off the end of the strip, and the
            // keyboard is the only route to it that does not involve reading a list first.
            //
            // Outside the edit-mode guard on purpose — switching document is not an edit — but not over a
            // text field, where Tab belongs to the field.
            //
            // ImGui BINDS Ctrl+Tab ITSELF (NavUpdateWindowing, enabled by NavEnableKeyboard) and it runs in
            // NewFrame, before this layer draws — so both would fire on one press: ImGui's window-ring
            // overlay AND this. The overlay is cancelled here rather than the key being fought for, and
            // ONLY when there was a document to switch to: with no documents open, Ctrl+Tab keeps ImGui's
            // ordinary window ring, which is a reasonable thing for it to do and not ours to remove.
            if ( io.KeyCtrl && !io.WantTextInput && ::ImGui::IsKeyPressed( ImGuiKey_Tab, false ) )
            {
                const SubjectId before = m_FocusedDocument;
                CycleDocuments();
                if ( m_FocusedDocument != before )
                    ::ImGui::GetCurrentContext()->NavWindowingTarget = nullptr;
            }

            // The ring is committed when Ctrl comes back up, not on each press: see CycleDocuments.
            if ( m_CyclingDocuments && !io.KeyCtrl )
            {
                m_CyclingDocuments = false;
                m_Documents.Touch( m_FocusedDocument );
            }
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
            // 3: the centre is split and documents get a node of their own (layout option B.1).
            constexpr int kDockLayoutVersion = 3;
            if ( EditorPreferences::Get().DockLayoutVersion < kDockLayoutVersion )
            {
                // SAID OUT LOUD. Every existing imgui.ini is rebuilt once here, and a layout that changes
                // in silence is read as the editor having lost the user's panels — which is the same
                // complaint an area that collapses on its own produces, and the reason B.1 does not
                // collapse. One line naming the old and new versions is the difference between "my layout
                // was reset by the update" and "my layout is gone".
                LOG_INFO( "[Editor] Docking layout rebuilt once: saved layout is version {}, this build lays "
                          "out version {} (the centre column now holds the level on the left and a Documents "
                          "area on the right). Your named layouts under View -> Layouts are untouched.",
                          EditorPreferences::Get().DockLayoutVersion, kDockLayoutVersion );

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

                //  ┌───────────┬────────────────┬───────────┬──────────────┐
                //  │ Scene     │                │           │ Details      │
                //  │ Outliner  │ Scene(viewport)│ Documents ├──────────────┤
                //  ├───────────┤                │           │ SceneSettings│
                //  │Collections├────────────────┴───────────┤ / Profiler   │
                //  │           │ Assets / Logs              │ / Foliage    │
                //  └───────────┴────────────────────────────┴──────────────┘
                //
                // THE DOCUMENT AREA IS A NODE, NOT A SET OF FLOATING WINDOWS (option B.1). The level never
                // leaves the screen: change a roughness in a material document and the crate in the viewport
                // beside it re-renders. It is paid for out of the centre's width permanently, whether or not
                // anything is open, and that permanence is the feature — an area that appeared and vanished
                // with the last document would resize the viewport under the user's cursor, which is what
                // people report as "the editor lost my panel". The splitter between the two is draggable
                // like every other, so a session that wants the width back can take it.
                ImGuiID center = dockspace_id;
                ImGuiID right  = ::ImGui::DockBuilderSplitNode( center, ImGuiDir_Right, 0.20f, nullptr, &center );
                ImGuiID left   = ::ImGui::DockBuilderSplitNode( center, ImGuiDir_Left, 0.22f, nullptr, &center );
                ImGuiID bottom = ::ImGui::DockBuilderSplitNode( center, ImGuiDir_Down, 0.28f, nullptr, &center );
                m_BottomDockId = bottom; // remembered so the drawer can be collapsed/restored later
                // Split AFTER the bottom drawer, so Assets/Logs still span the whole centre rather than
                // only the level's half of it.
                ImGuiID documents =
                     ::ImGui::DockBuilderSplitNode( center, ImGuiDir_Right, 0.44f, nullptr, &center );
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
                // No line for "Anim Graph" or "Particle Editor": they are documents, and a document does
                // not have a fixed home in the layout — it docks into the document well beside the others
                // (DrawDocuments sets the dock id), which is the whole point of the well existing.
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "UI Editor" ).c_str(), right );
                ::ImGui::DockBuilderDockWindow( PanelDisplayTitle( "Modeling" ).c_str(), left );

                // The well itself. It is what makes the document node FINDABLE: a dock node with nothing in
                // it is not drawn at all, so without a permanent occupant the area would exist in the
                // layout and be invisible on screen the whole time no document was open. It is also where
                // every document reads its dock id from at runtime — see DrawDocumentWell.
                ::ImGui::DockBuilderDockWindow( kDocumentWellWindow, documents );

                ::ImGui::DockBuilderFinish( dockspace_id );
            }
        }

        // THE TOOLS. The document loop is DrawDocuments, below, and the two are separate for the reason the
        // whole task exists: a tool passes &GetVisibility() to Begin, which is right for a setting the user
        // keeps, and a document must not — its false would be read as "destroy this window".
        //
        // The cascade this loop used to carry for documents is gone with them: a document is DOCKED into the
        // well now, so there is no floating window to step down-right from the last one.
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
                ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver,
                                         ImVec2( 0.5f, 0.5f ) );
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

        // The well BEFORE the documents: it reads back the dock node id the documents are about to be
        // docked into, and a document opened this frame would otherwise float once and settle next frame.
        DrawDocumentWell();
        DrawDocuments();

        DrawProfilerWindow();

        DrawStatusBar();

        DrawCommandPalette();
        DrawRecoveryPopup();
        DrawLayoutSavePopup();
        DrawOpenRefusedPopup();

        // Transient bottom-right notifications (save/import/validation). Drawn last so they float on top.
        Editor::ToastManager::Get().Draw();

        ::ImGui::End(); // End dockspace

#ifdef EBABLE_IMGUI
        m_ImGuiLayer->End();
#endif

        // AFTER the interface has been recorded into the swapchain pass and BEFORE the frame is submitted:
        // the only window in which the presented image is legally ours to copy out of. A no-op unless a
        // `shot.window` is waiting on exactly this frame. See RecordWindowCaptureIfDue.
        RecordWindowCaptureIfDue();

        return BOOLSUCCESS;
    }

    // Defined with the Open Scene popup's other helpers, further down this file; declared here because the
    // command palette names its scene entries the same way that popup does, and one naming rule is the
    // point — a level offered as "Arena.desce" in one list and "Levels/Arena.desce" in the other is two
    // names for one thing, and the channel would then have a name the UI never shows.
    static std::string SceneLabel( const Common::Filepath& path );

    std::vector<PaletteCommand> EditorLayer::BuildPaletteCommands()
    {
        std::vector<PaletteCommand> commands;
        commands.reserve( m_Panels.Size() + m_Documents.Count() + 32 );

        // Panels — jump to / reveal any tool window. TOOLS ONLY, and by construction rather than by a
        // filter: m_Panels is a PanelRegistry, which cannot hold a document. Before the split this loop
        // offered "Open M_Crate_Painted###assetdoc..." as a panel, and running it set a visibility flag that
        // the close pass then read as "the user dismissed this window".
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

        // Documents — FOCUS an open one. A separate category because the verb is different and the
        // difference is the point of this task: a tool is opened, a document is switched to. Nothing here
        // creates or destroys a window, so a mistyped search cannot cost the user one.
        for ( const auto& document : m_Documents )
        {
            const SubjectId subject = document->Subject();
            commands.push_back( { "Document", "Go to " + DocumentDisplayName( document->GetName() ),
                                  [this, subject] { FocusDocument( subject ); } } );
        }

        // Closing one, by name. Never offered before, because a person closes a window with the x on it —
        // which is exactly the gesture no unattended run can make, and therefore the reason "close a
        // document and show what the well offers back" was a claim nobody could photograph. It goes
        // through RequestDocumentClose like the x does, so the destruction still happens between frames
        // behind the device-idle wait.
        for ( const auto& document : m_Documents )
        {
            const SubjectId subject = document->Subject();
            commands.push_back( { "Document", "Close " + DocumentDisplayName( document->GetName() ),
                                  [this, subject]
                                  {
                                      RequestDocumentClose( subject, "closed from the command "
                                                                     "palette" );
                                  } } );
        }

        // Ctrl+Tab, as a command. The key is bound in OnImGuiRender and a key is not available to a
        // client either; this is the same CycleDocuments the keystroke calls, so the ring the two walk
        // cannot differ.
        if ( m_Documents.Count() > 1 )
        {
            commands.push_back(
                 { "Document", "Cycle to the next most recently used", [this] { CycleDocuments(); } } );
        }

        // Reopening one that was closed. The list the empty well shows, reachable without a mouse — and
        // it is the same Core::SubjectOpenRequests the Selectable there uses, so a reopen is refused by the
        // six-slot cap exactly like any other open rather than becoming a second way in.
        for ( const ClosedDocument& closed : m_Documents.RecentlyClosed() )
        {
            const SubjectId subject = closed.Subject;
            commands.push_back( { "Document", "Reopen " + closed.DisplayName,
                                  [subject] { Core::SubjectOpenRequests::Request( subject ); } } );
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
                commands.push_back( { "Entity", name, [uuid] { Core::SelectionManager::SetSelected( uuid ); } } );

                // DELETING ONE IS ALSO SOMETHING A PERSON DOES, and until now the palette could only
                // SELECT. The Outliner's context menu and the Delete key both reach
                // Commands::DeleteEntity — the same undoable command this runs — so the capability
                // was always there and only the dictionary entry was missing.
                //
                // FOUND BY NEEDING IT. Verifying "a document closes with its subject" through the control
                // channel means killing a subject through the control channel, and there was no way to
                // destroy an entity without a mouse: the channel runs these closures and nothing else. A
                // gap in the palette is a gap in what an agent can do at all, which is the one claim the
                // palette exists to make good on.
                commands.push_back( { "Entity", "Delete " + name, [uuid] { Commands::DeleteEntity( uuid ); } } );

                // ── AND WHAT CAN BE OPENED *FROM* THIS ENTITY ─────────────────────────────────────────
                //
                // The other half of U7, and the half that makes a component document reachable at all
                // without a mouse. The Details panel's button is how a person opens one; this is the same
                // request under a name, which is what puts it in THE DICTIONARY — the palette, and
                // therefore the control channel, which runs these same closures.
                //
                // A DOCUMENT REACHABLE ONLY BY CLICKING A BUTTON IS MISSING FROM THAT DICTIONARY, and the
                // dictionary is this editor's one claim that "anything a person can do, an agent can do".
                // The asset documents already had their entry (the Open group below, over the registered
                // assets); a subject that is not a file had none, because there was no file to enumerate.
                // Enumerating the ENTITIES against the registered COMPONENT kinds is the same loop over
                // the other domain.
                //
                // DERIVED FROM THE REGISTRY, never a hand-written list of the two kinds that exist today:
                // a third component document appears here the moment its factory is registered, which is
                // the census this task exists to stop anybody having to refill.
                for ( const SubjectTypeKey& type : m_SubjectEditors.RegisteredTypes() )
                {
                    if ( type.Domain != SubjectDomain::EntityComponent )
                        continue;

                    const SubjectId subject{ type.Domain, type.Facet, uuid };
                    if ( !m_SubjectEditors.Exists( subject ) )
                        continue;

                    commands.push_back( { "Open", name + " \xc2\xb7 " + m_SubjectEditors.TypeName( type ),
                                          [subject] { Core::SubjectOpenRequests::Request( subject ); } } );
                }
            }
        }

        // THE MENU BAR. `--open-menu` is gone and this is where its capability went: a menu can be opened,
        // photographed and closed again, as many times as a session likes, instead of being pinned open
        // for a whole run by a flag with no way to say "now let go".
        for ( const char* menu : kMenuBarMenus )
        {
            const std::string name = menu;
            commands.push_back(
                 { "Menu", "Open the " + name + " menu", [this, name] { m_HeldOpenMenu = name; } } );
        }
        commands.push_back( { "Menu", "Close the open menu", [this] { m_HeldOpenMenu.clear(); } } );

        // THE SNAP, AND THE PERF HUD. Both are things a person does with a single click and neither had a
        // name, so neither could be done unattended — and a gap in this dictionary is a gap in what an
        // agent can do at all, which is the claim the palette exists to make good on. Found by needing
        // them: К6 moved the snap step to one owner and then could not photograph the defect it fixed,
        // because the sequence is "set a step, do something unrelated, look" and the channel could reach
        // neither half. The three toolbar popups and the View -> Show menu were the only ways in.
        //
        // THE STEPS ARE THE TOOLBAR'S OWN LISTS, not a copy: kGridSteps and kAngleSteps are declared once
        // at the top of this file and read by DrawSnapControl as well, so a step added there appears here
        // and the two can never offer different menus.
        //
        // Labels are ASCII on purpose. A client addresses a command by its exact label over the control
        // channel (`desertctl run Snap "Angle snap 15 deg"`), and the degree sign the toolbar button draws
        // is two UTF-8 bytes that a shell argument carries badly.
        for ( const float step : kGridSteps )
        {
            char label[48];
            if ( step >= 100.0f )
                std::snprintf( label, sizeof( label ), "Grid snap %.0f m", step / 100.0f );
            else
                std::snprintf( label, sizeof( label ), "Grid snap %.0f cm", step );
            commands.push_back( { "Snap", label, [step] { Core::GizmoState::SetTranslateSnap( step ); } } );
        }
        for ( const float step : kAngleSteps )
        {
            char label[48];
            std::snprintf( label, sizeof( label ), "Angle snap %.0f deg", step );
            commands.push_back( { "Snap", label, [step] { Core::GizmoState::SetRotateSnapDegrees( step ); } } );
        }
        commands.push_back( { "Snap", "Toggle snapping", []
                              { Core::GizmoState::SetPersistentSnap( !Core::GizmoState::PersistentSnap() ); } } );

        // The View -> Show item, under a name. It is the cheapest action in the editor that saves the
        // preferences file while having nothing whatever to do with the gizmo, which is exactly what makes
        // it the other half of К6's scenario — and it is a dictionary entry in its own right, since
        // "turn the frame timings on" is something a person asks for by name.
        commands.push_back( { "Action", "Toggle the Perf HUD", []
                              {
                                  EditorPreferences::Get().ShowPerfHud = !EditorPreferences::Get().ShowPerfHud;
                                  EditorPreferences::Save();
                              } } );

        // OPENABLE ASSETS. This is where `--open-panel <path-to-asset>` went — the half of that flag that
        // opened a DOCUMENT rather than a tool, and the only way a document has ever been put on screen
        // unattended, since a document does not exist until something opens its asset and therefore has
        // no name to be reached by.
        //
        // The list is the project's registered assets filtered by "does an editor open this kind", which
        // is m_SubjectEditors and not a hand-written type list — so a new document type appears here the
        // moment its factory is registered.
        if ( m_AssetManager )
        {
            for ( const auto& [metadata, asset] : m_AssetManager->RegisteredAssets() )
            {
                const SubjectId subject =
                     AssetSubject( metadata.Handle, static_cast<uint32_t>( metadata.AssetType ) );
                if ( !metadata.IsValid() || !m_SubjectEditors.HasEditorFor( subject.Type() ) )
                    continue;

                commands.push_back( { "Open", metadata.Filepath.filename().generic_string(),
                                      [subject] { Core::SubjectOpenRequests::Request( subject ); } } );
            }
        }

        // THE LEVELS, which every other kind of document could already be opened by name from here and a
        // level could not — the one thing an editor exists to open was the one thing the palette had no
        // entry for, and therefore the one thing the control channel could not ask for either (the
        // channel's vocabulary IS this list). A separate group from "Open" above because these are not
        // documents: opening one REPLACES the world rather than adding a tab.
        //
        // Routed through SceneOpenRequest, not through LoadScene, on purpose: that is the path that runs
        // the unsaved-changes gate, and a palette entry is at least as easy to hit by accident as the
        // drag-and-drop it was written for.
        for ( const Common::Filepath& scene : CollectAvailableScenes() )
        {
            const std::string path = scene.string();
            commands.push_back( { "Scene", "Open Scene " + SceneLabel( scene ),
                                  [path] { Editor::Core::SceneOpenRequest::Request( path ); } } );
        }

        // NAMED VIEWPOINTS for the focused document's preview — the replacement for `--preview-orbit
        // yaw,pitch`, whose continuous angle pair a palette entry has nowhere to carry. See
        // Editor/Core/PreviewViewpoints.hpp for why names are MORE reproducible than numbers, not less.
        //
        // Offered for the FOCUSED document only, because that is the one a person means by "the preview"
        // and because seven entries per open document would bury everything else in the list.
        if ( ISubjectDocument* focused = m_Documents.Find( m_FocusedDocument ); focused && focused->HasPreview() )
        {
            for ( const PreviewViewpoint& viewpoint : kPreviewViewpoints )
            {
                const PreviewViewpoint* aim = &viewpoint;
                commands.push_back( { "Preview", std::string( viewpoint.Name ), [this, aim]
                                      {
                                          // Re-resolved rather than captured: the focus can move, and the
                                          // document can be destroyed, between this list being built and
                                          // the entry being run.
                                          if ( ISubjectDocument* target = m_Documents.Find( m_FocusedDocument );
                                               target && target->HasPreview() )
                                          {
                                              target->SetPreviewViewpoint( *aim );
                                          }
                                      } } );
            }
        }

        // THE THREE STATES OF THE FOCUSED DOCUMENT, as ordinary commands.
        //
        // Apply, Discard and Save are ACTIONS with names — they belong in the palette by the same rule
        // that put "Save Scene" there, and putting them here rather than inventing channel operations for
        // them is what keeps the channel's vocabulary the palette's vocabulary. The artist gets them on
        // the keyboard as a side effect, which is the argument for the palette in the first place.
        //
        // APPLY AND DISCARD ARE OFFERED ONLY WHILE THERE IS SOMETHING TO APPLY. The palette lists what is
        // available THIS INSTANT, exactly as the toolbar disables the two buttons in the same state; an
        // entry that ran and did nothing would be a silent no-op reported as a success, and a client
        // would read it as "the scene now has my edit".
        if ( ISubjectDocument* focused = m_Documents.Find( m_FocusedDocument ) )
        {
            const SubjectId subject = m_FocusedDocument;

            if ( focused->GetEditModel() == ISubjectDocument::EditModel::Staged && focused->HasUnappliedEdits() )
            {
                // Re-resolved inside, not captured: the focus can move and the document can be destroyed
                // between this list being built and the entry being run — the same rule the Preview
                // viewpoints above follow, for the same reason.
                commands.push_back( { "Document", "Apply this document's edits to the scene", [this, subject]
                                      {
                                          if ( ISubjectDocument* target = m_Documents.Find( subject ) )
                                              (void)target->ApplyEdits();
                                      } } );
                commands.push_back( { "Document", "Discard this document's unapplied edits", [this, subject]
                                      {
                                          if ( ISubjectDocument* target = m_Documents.Find( subject ) )
                                              (void)target->DiscardEdits();
                                      } } );
            }

            commands.push_back( { "Document", "Save this document", [this, subject]
                                  {
                                      if ( ISubjectDocument* target = m_Documents.Find( subject ) )
                                          (void)target->SaveDocument();
                                  } } );
        }

        // Actions.
        commands.push_back( { "Action", "Save Scene", [this] { (void)SaveOpenScene(); } } );
        commands.push_back( { "Action", "Undo", [] { CommandHistory::Get().Undo(); } } );
        commands.push_back( { "Action", "Redo", [] { CommandHistory::Get().Redo(); } } );
        commands.push_back( { "Action", "Close All Documents", [this] { RequestCloseAllDocuments(); } } );

        return commands;
    }

    void EditorLayer::DrawCommandPalette()
    {
        if ( !m_CommandPalette.IsOpen() )
            return;

        m_CommandPalette.SetCommands( BuildPaletteCommands() );
        m_CommandPalette.Draw();
    }

    void EditorLayer::DrawDocumentWell()
    {
        namespace ImGui = ::ImGui;

        // No p_open: THE AREA DOES NOT CLOSE AND DOES NOT COLLAPSE WHEN IT EMPTIES. The alternative was
        // drawn and rejected — a node that appears and disappears gives the viewport its width back and
        // takes it away again, resizing the level view under the user's cursor, and a layout that moves on
        // its own is what people report as "the editor lost my panel". The splitter is draggable: a session
        // that wants the pixels can take them, deliberately and once.
        ImGui::Begin( kDocumentWellWindow, nullptr, ImGuiWindowFlags_NoCollapse );

        // READ BACK, not remembered. The id is only known at DockBuilder time in the ONE session that built
        // the layout; every later session loads it from imgui.ini and a captured value would be 0 — which is
        // the bug the bottom drawer's own m_BottomDockId still has. Asking the window where it is docked
        // gives the same answer in every session, including after the user drags the well somewhere else.
        m_DocumentDockId = ImGui::GetWindowDockID();

        if ( m_Documents.Empty() )
        {
            // THE EMPTY STATE SAYS WHAT THE AREA IS FOR. A reserved column that is blank most of the time
            // is a column nobody learns the purpose of; this is the price B.1 pays for stable geometry and
            // it is paid in words rather than in pixels.
            const float avail = ImGui::GetContentRegionAvail().x;

            ImGui::Dummy( ImVec2( 0.0f, 24.0f ) );
            {
                // The DEFAULT font, not the bold one: the icon range is merged into the default face only,
                // so the same glyph drawn in bold comes out as the missing-glyph box. (Measured — the first
                // capture of this empty state had a "?" where the document icon belongs.)
                const char* icon = ICON_MDI_FILE_DOCUMENT_OUTLINE;
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( avail - ImGui::CalcTextSize( icon ).x ) * 0.5f );
                ImGui::TextDisabled( "%s", icon );
            }

            ImGui::Dummy( ImVec2( 0.0f, 8.0f ) );
            {
                const char* title = "No document open";
                ImGui::PushFont( EditorResources::GetBoldFont() );
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( avail - ImGui::CalcTextSize( title ).x ) * 0.5f );
                ImGui::TextUnformatted( title );
                ImGui::PopFont();
            }

            ImGui::Dummy( ImVec2( 0.0f, 6.0f ) );
            {
                // Both doors named, because both exist and neither is discoverable from an empty area:
                // the browser's double-click and the pencil on an asset slot in Details.
                const char* body = "Double-click a material, a cloud type, a noise volume or a layout in the "
                                   "Content Browser \xe2\x80\x94 or press the pencil on any asset slot in "
                                   "Details.";
                ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + avail );
                ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
                ImGui::TextUnformatted( body );
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
            }

            ImGui::Dummy( ImVec2( 0.0f, 10.0f ) );
            {
                const char*  label = ICON_MDI_FOLDER_MULTIPLE_OUTLINE "  Browse assets";
                const ImVec2 size( ImGui::CalcTextSize( label ).x + ImGui::GetStyle().FramePadding.x * 2.0f,
                                   0.0f );
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( avail - size.x ) * 0.5f );
                if ( ImGui::Button( label, size ) )
                    Core::PanelRequests::Open( "Assets" );
            }

            // RECENTLY CLOSED: the one thing an area that stays can offer that a vanishing one cannot.
            // Reopening goes through the ordinary open request, so it is refused by the slot cap exactly
            // like any other open and cannot become a second way in.
            if ( !m_Documents.RecentlyClosed().empty() )
            {
                ImGui::Dummy( ImVec2( 0.0f, 12.0f ) );
                ImGui::Separator();
                ImGui::TextDisabled( "RECENTLY CLOSED" );
                for ( const ClosedDocument& closed : m_Documents.RecentlyClosed() )
                {
                    ImGui::PushID( static_cast<int>( std::hash<SubjectId>{}( closed.Subject ) & 0x7fffffff ) );
                    const std::string row =
                         std::string( m_SubjectEditors.Icon( closed.Subject, kUnknownDocumentIcon ) ) + "  " +
                         closed.DisplayName;
                    if ( ImGui::Selectable( row.c_str() ) )
                        Core::SubjectOpenRequests::Request( closed.Subject );
                    if ( ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "Reopen this %s document",
                                           m_SubjectEditors.TypeName( closed.Subject ).c_str() );
                    ImGui::PopID();
                }
            }

            ImGui::End();
            return;
        }

        // SOMETHING IS OPEN: the well becomes the INDEX of the area it names. Past about six documents the
        // tab strip has the one you want off its end, so a list is not a fallback here — it is the primary
        // way to switch, and it carries the two facts a tab cannot: which type each document is, and
        // whether it is holding one of the six renderer slots.
        ImGui::TextDisabled( "OPEN DOCUMENTS \xe2\x80\x94 %zu", m_Documents.Count() );
        ImGui::Separator();

        // Most recently used first, the same order Ctrl+Tab walks — one order, read in two places, so the
        // list cannot teach a different sequence from the key.
        std::vector<SubjectId> closeRequests;
        for ( const SubjectId& subject : m_Documents.MostRecentOrder() )
        {
            const ISubjectDocument* document = m_Documents.Find( subject );
            if ( !document )
                continue;

            ImGui::PushID( static_cast<int>( std::hash<SubjectId>{}( subject ) & 0x7fffffff ) );

            const std::string row =
                 std::string( m_SubjectEditors.Icon( document->Subject(), kUnknownDocumentIcon ) ) + "  " +
                 DocumentDisplayName( document->GetName() );
            if ( ImGui::Selectable( row.c_str(), subject == m_FocusedDocument,
                                    ImGuiSelectableFlags_AllowItemOverlap ) )
                FocusDocument( subject );

            // The slot column. "Cloud - no slot" is not trivia: it is the answer to "I closed four windows
            // and it still will not open", because closing a CPU-drawn document frees nothing.
            const char*       slot = document->HoldsRendererSlot()    ? "1 slot"
                                     : document->ClaimsRendererSlot() ? "claiming"
                                                                      : "no slot";
            const std::string right  = m_SubjectEditors.TypeName( document->Subject() ) + " \xc2\xb7 " + slot;
            const float rightW = ImGui::CalcTextSize( right.c_str() ).x;
            ImGui::SameLine( ImGui::GetContentRegionMax().x - rightW - 28.0f );
            ImGui::TextDisabled( "%s", right.c_str() );

            ImGui::SameLine( ImGui::GetContentRegionMax().x - 18.0f );
            if ( ImGui::SmallButton( ICON_MDI_CLOSE ) )
                closeRequests.push_back( subject );

            ImGui::PopID();
        }

        ImGui::End();

        // Requested after the loop: RequestDocumentClose only queues, but collecting first keeps the rule
        // that nothing mutates a container while it is being walked.
        for ( const SubjectId& subject : closeRequests )
            RequestDocumentClose( subject, "closed from the Documents index" );
    }

    void EditorLayer::DrawDocuments()
    {
        namespace ImGui = ::ImGui;

        std::vector<SubjectId> closeRequests;
        SubjectId              focused;

        for ( const auto& document : m_Documents )
        {
            const SubjectId subject = document->Subject();

            // A DOCKED DOCUMENT, not a floating one. Before this they opened as a cascade of floating
            // windows stepped 32 px down-right from each other, which is what an application does when it
            // has nowhere to put them; option B.1 gives them somewhere. FirstUseEver, so a document the user
            // has since dragged out stays where they put it.
            if ( m_DocumentDockId != 0 )
                ImGui::SetNextWindowDockID( m_DocumentDockId, ImGuiCond_FirstUseEver );
            if ( const ImVec2 defSize = document->GetDefaultSize(); defSize.x > 0.0f && defSize.y > 0.0f )
                ImGui::SetNextWindowSize( defSize, ImGuiCond_FirstUseEver );

            if ( !m_FocusPanel.empty() && document->GetName() == m_FocusPanel )
            {
                ImGui::SetNextWindowFocus();
                m_FocusPanel.clear();
            }

            // THE CLOSE BOX WRITES TO A FRAME-LOCAL BOOL, NOT TO THE PANEL'S VISIBILITY.
            //
            // This one line is the defect, fixed. While documents lived in the panel list they were drawn
            // with `&panel->GetVisibility()` like every tool, so one bool meant "hidden" for a tool and
            // "destroy me" for a document — and the View menu, which wrote that same bool, could therefore
            // destroy a document with a tick and had no way to bring it back. A document has no visibility:
            // it is open, or it does not exist.
            bool open = true;
            ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, document->GetWindowPadding() );
            // BEGIN'S RETURN VALUE IS "IS THIS DOCUMENT ON SCREEN", and it was being thrown away. It is
            // false for a window that is collapsed and for one whose dock tab is not the active one — so
            // four documents in one dock node were all drawing their contents every frame while one of
            // them was visible, and the three that were not were also holding renderer slots for it. The
            // content is skipped, which is ImGui's own idiom, and the frames off screen are counted so the
            // slot can go back (ReleaseSlotsOfHiddenDocuments).
            const bool visible = ImGui::Begin( DocumentDisplayTitle( *document ).c_str(), &open );
            ImGui::PopStyleVar();
            if ( visible )
            {
                if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) )
                    focused = subject;
                {
                    DESERT_PROFILE_SCOPE_DYNAMIC( document->GetName().c_str() );
                    document->OnUIRender();
                }
                // RESET RATHER THAN DECREMENTED. The threshold is about a window the user has LEFT off
                // screen; one visible frame means they have not, and counting down from thirty would make
                // the release depend on how often they flicked back to it.
                m_DocumentHiddenFrames.erase( subject );
            }
            else
            {
                ++m_DocumentHiddenFrames[subject];
            }
            ImGui::End();

            if ( !open )
                closeRequests.push_back( subject );
        }

        // The focus is only MOVED by a document that actually has it. A frame in which the keyboard is on a
        // tool leaves the last focused document standing, so Ctrl+Tab resumes from where the user was
        // editing rather than from nothing.
        if ( !focused.IsNull() )
        {
            // CLICKING A DOCUMENT COMMITS THE RING, cycling to one does not. Both are "focus", so without
            // this distinction one of the two rules would be wrong: either a mouse click would leave
            // Ctrl+Tab walking an order the user has since abandoned, or the second Ctrl+Tab would return to
            // where the first one started. The cycling flag is cleared when Ctrl comes up, and the ring is
            // committed there — see the shortcut block in OnImGuiRender.
            if ( focused != m_FocusedDocument && !m_CyclingDocuments )
                m_Documents.Touch( focused );

            m_FocusedDocument = focused;
        }

        for ( const SubjectId& subject : closeRequests )
            RequestDocumentClose( subject, "you closed the window" );
    }

    void EditorLayer::DrawOpenRefusedPopup()
    {
        namespace ImGui = ::ImGui;

        constexpr const char* kTitle = "Cannot open this document";

        if ( m_OpenRefusalPending )
        {
            ImGui::OpenPopup( kTitle );
            m_OpenRefusalPending = false;
        }

        ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                 ImVec2( 0.5f, 0.5f ) );

        if ( !ImGui::BeginPopupModal( kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
            return;

        if ( !m_OpenRefusal )
        {
            // Cannot normally happen; the modal is only ever opened with a refusal in hand. Closing rather
            // than drawing an empty dialog, because an empty dialog with no way out is worse than none.
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetErrorColor() );
        ImGui::TextUnformatted( ICON_MDI_ALERT_CIRCLE_OUTLINE );
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushFont( EditorResources::GetBoldFont() );
        ImGui::Text( "Cannot open %s", m_OpenRefusal->AssetName.c_str() );
        ImGui::PopFont();

        ImGui::TextDisabled( "All %u renderer slots are in use%s. Close one of these to free one:",
                             EngineContext::kMaxRendererSlots,
                             m_OpenRefusal->Pending > 0 ? " or already committed" : "" );
        ImGui::Separator();

        std::vector<SubjectId> closeRequests;
        for ( const RendererSlotConsumer& consumer : m_OpenRefusal->Census )
        {
            ImGui::TextUnformatted( consumer.Name.c_str() );

            // A row the user can act on gets a button; the main viewport and the Details preview do not,
            // because neither is a window a person closes to make room. Saying nothing on those rows is
            // the honest version: they are named because they explain where the slots went.
            if ( consumer.Document && m_Documents.Find( *consumer.Document ) )
            {
                ImGui::SameLine( ImGui::GetContentRegionMax().x - 64.0f );
                ImGui::PushID( static_cast<int>( std::hash<SubjectId>{}( *consumer.Document ) & 0x7fffffff ) );
                if ( ImGui::SmallButton( "Close" ) )
                    closeRequests.push_back( *consumer.Document );
                ImGui::PopID();
            }

            // The CPU-drawn documents say so, for the reason the log line already did: closing one frees
            // nothing, and a census that let the user close four of them and still be refused would be a
            // longer way of saying nothing.
            if ( !consumer.HoldsSlot && !consumer.ClaimsSlot )
            {
                ImGui::Indent( 18.0f );
                ImGui::TextDisabled( "drawn on the CPU \xe2\x80\x94 closing it frees nothing" );
                ImGui::Unindent( 18.0f );
            }
        }

        ImGui::Separator();
        if ( ImGui::Button( "Close this message", ImVec2( 180.0f, 0.0f ) ) )
        {
            m_OpenRefusal.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled( "The same census is in the log." );

        ImGui::EndPopup();

        for ( const SubjectId& subject : closeRequests )
            RequestDocumentClose( subject, "closed to free a renderer slot" );
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
                if ( !LayoutManager::Save( m_LayoutNameBuf ) )
                    Editor::ToastManager::Push( "The layout was not saved (see the log)",
                                                Editor::ToastLevel::Error );
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

        // THE MENU HELD OPEN, if the control channel asked for one. `--open-menu <name>` stood here and
        // it could hold a menu open for the whole run and never let go, because a flag has no later
        // moment at which to be told otherwise. "Menu" / "Open the View menu" is now an ordinary palette
        // entry, so a session can photograph a menu and then close it and carry on.
        //
        // OpenPopup here and BeginMenu below derive the same id from the same label in the same window
        // (BeginMenu: window->GetID(label); OpenPopup: CurrentWindow->GetID(str_id)), which is what makes
        // this the menu's own opening rather than a second popup wearing its name. Re-issued every frame
        // because a menu closes as soon as focus leaves it and a shot may land on any frame.
        //
        // The name was validated against kMenuBarMenus when the command was built, so there is no unknown
        // name to reject here: the palette cannot offer one.
        if ( !m_HeldOpenMenu.empty() )
            ImGui::OpenPopup( m_HeldOpenMenu.c_str() );

        DrawFileMenu();
        DrawEditMenu();
        DrawViewMenu();
        DrawWindowMenu();
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

    std::vector<Common::Filepath> EditorLayer::CollectAvailableScenes()
    {
        std::vector<Common::Filepath> scenes;

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
                scenes.push_back( it->path() );
        }

        // Sorted by the label the list shows, which keeps every folder's scenes contiguous (they share the
        // "Folder/" prefix) — that is what the folder headers in the popup rely on.
        std::sort( scenes.begin(), scenes.end(), []( const Common::Filepath& a, const Common::Filepath& b )
                   { return SceneLabel( a ) < SceneLabel( b ); } );
        return scenes;
    }

    void EditorLayer::PrepareScenePopup()
    {
        m_AvailableScenes    = CollectAvailableScenes();
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

    uint64_t EditorLayer::SceneTriangleCount()
    {
        const uint64_t revision    = CommandHistory::Get().Revision();
        const size_t   entityCount = m_MainScene->GetAllEntities().size();

        // Recompute on an edit, on the population changing, or every ~120 frames — the last one because an
        // async mesh load completes without touching either of the other two, and a status bar stuck on
        // "0 tris" while the scene is visibly full would be worse than showing no number at all.
        constexpr int kMaxCacheAgeFrames = 120;
        if ( revision == m_TriangleCacheRev && entityCount == m_TriangleCacheCount &&
             ++m_TriangleCacheAge < kMaxCacheAgeFrames )
        {
            return m_TriangleCache;
        }

        uint64_t total = 0;
        for ( const ECS::Entity& entity : m_MainScene->GetAllEntities() )
        {
            // HIDDEN entities are excluded: the number sits beside the entity count in a bar that answers
            // "what is on screen", and a hidden mesh is not.
            if ( entity.HasComponent<ECS::VisibilityComponent>() &&
                 !entity.GetComponent<ECS::VisibilityComponent>().Visible )
            {
                continue;
            }
            // ResolveDrawnMesh, not the mesh handle: a primitive draws the process-wide shared mesh and has
            // no handle at all, and counting only handles reports zero for a scene of cubes (the exact trap
            // that helper documents).
            if ( const ::Desert::Mesh* mesh = ResolveDrawnMesh( entity ) )
                total += Geometry::ComputeMeshStats( mesh->GetSubmeshes() ).Triangles;
        }

        m_TriangleCache      = total;
        m_TriangleCacheRev   = revision;
        m_TriangleCacheCount = entityCount;
        m_TriangleCacheAge   = 0;
        return total;
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
        ImGui::TextDisabled( ICON_MDI_SHAPE " %zu entities", m_MainScene->GetAllEntities().size() );

        // What the scene costs to draw, beside what it contains. Two numbers that belong together: an
        // entity count says how much there is to manage, a triangle count says how much there is to
        // render, and only the second one explains a frame time.
        ImGui::SameLine( 0.0f, 16.0f );
        {
            const uint64_t tris = SceneTriangleCount();
            ImGui::TextDisabled( ICON_MDI_TRIANGLE_OUTLINE " %s tris", FormatThousands( tris ).c_str() );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Triangles in the VISIBLE meshes of this scene (LOD 0)." );
        }

        // HOW MANY DOCUMENTS, AND HOW MANY OF THE SIX SLOTS ARE GONE. Both numbers already existed in the
        // code — GetLiveRendererCount and PendingRendererSlotDemand — and neither had anywhere to appear,
        // so the first a user heard of the cap was a click that did nothing. A count of documents is not
        // the number that matters; the slot census is, which is why they are shown together: three
        // documents can be three slots or none, depending on which three.
        ImGui::SameLine( 0.0f, 16.0f );
        {
            const uint32_t live    = Graphic::SceneRenderer::GetLiveRendererCount();
            const uint32_t pending = PendingRendererSlotDemand( m_Documents.Documents() );

            // ImGuiCol_TextDisabled, not ImGuiCol_Text: the line below is drawn with TextDisabled like the
            // rest of the bar, and pushing the wrong colour would leave it grey with a colour nobody sees.
            const bool tight = live + pending >= EngineContext::kMaxRendererSlots;
            if ( tight )
                ImGui::PushStyleColor( ImGuiCol_TextDisabled, ThemeManager::GetWarningColor() );
            ImGui::TextDisabled( ICON_MDI_FILE_DOCUMENT_MULTIPLE_OUTLINE " %zu document%s \xc2\xb7 %u/%u slots",
                                 m_Documents.Count(), m_Documents.Count() == 1 ? "" : "s", live,
                                 EngineContext::kMaxRendererSlots );
            if ( tight )
                ImGui::PopStyleColor();

            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "%zu open document(s). %u of the %u renderer slots are in use and %u more "
                                   "are committed to documents that have not drawn yet; a document that "
                                   "needs one is refused when they are all spoken for.",
                                   m_Documents.Count(), live, EngineContext::kMaxRendererSlots, pending );
        }

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
                        // CENTIMETRES, and metres only past a metre — the same rule DrawSnapControl
                        // formats the toolbar button with, and it has to be the same rule because the two
                        // labels sit on one screen reading one value. This said "%.2fm" over a value that
                        // is in world units (1 unit = 1 cm), so a 5 m step read "500.00m" three inches
                        // from a button reading "5 m". Third sighting of У5's metre-era label: the field's
                        // default, the Preferences slider, and now the status bar.
                        if ( Gz::TranslateSnap() >= 100.0f )
                            ImGui::TextDisabled( ICON_MDI_MAGNET " %.0f m", Gz::TranslateSnap() / 100.0f );
                        else
                            ImGui::TextDisabled( ICON_MDI_MAGNET " %.0f cm", Gz::TranslateSnap() );
                        break;
                }
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Snap (toggle in the viewport toolbar; Ctrl inverts while dragging)" );
        }

        // Right: how much the log is complaining, then which build this is.
#ifdef DESERT_CONFIG_DEBUG
        constexpr const char* kBuildConfig = "Debug";
#else
        constexpr const char* kBuildConfig = "Release";
#endif
        const float fps = ImGui::GetIO().Framerate;
        // The BRANCH beside the version: this project runs eight worktrees at once, and "which of these
        // windows is my build" was previously answerable only from the commit hash. It comes from the same
        // build-time git identity the version does, so it cannot disagree with the hash beside it.
        char stats[220];
        std::snprintf( stats, sizeof( stats ),
                       ICON_MDI_SOURCE_BRANCH " %s   %s  %s   " ICON_MDI_SPEEDOMETER " %.0f FPS   %.2f ms",
                       Common::Version::Branch(), Common::Version::Full(), kBuildConfig, fps,
                       fps > 0.0f ? 1000.0f / fps : 0.0f );

        // "Are there warnings?" answered where you are already looking, without opening the log. The count
        // comes from the Logs panel's parse of the file — the one place that has read it — so the chip in
        // that panel and this number cannot disagree.
        const std::size_t warnings   = LogsPanel::WarningCount();
        const std::size_t errors     = LogsPanel::ErrorCount();
        char              alerts[96] = {};
        if ( errors > 0 )
            std::snprintf( alerts, sizeof( alerts ), ICON_MDI_CLOSE_CIRCLE_OUTLINE " %zu   " ICON_MDI_ALERT " %zu",
                           errors, warnings );
        else if ( warnings > 0 )
            std::snprintf( alerts, sizeof( alerts ), ICON_MDI_ALERT " %zu warnings", warnings );

        const bool  dirty   = CommandHistory::Get().Revision() != s_SavedRevision;
        const float starW   = dirty ? ImGui::CalcTextSize( "* " ).x : 0.0f;
        const float statsW  = ImGui::CalcTextSize( stats ).x;
        const float alertsW = alerts[0] ? ImGui::CalcTextSize( alerts ).x + 16.0f : 0.0f;
        ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - statsW - starW - alertsW );

        if ( alerts[0] )
        {
            // Errors outrank warnings in the colour as well as in the text: one red count is the whole
            // signal, and painting it amber because warnings are also present would bury it.
            ImGui::TextColored( errors > 0 ? ThemeManager::GetErrorColor() : ThemeManager::GetWarningColor(), "%s",
                                alerts );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "%zu error(s), %zu warning(s) in this session's log — click to open it.",
                                   errors, warnings );
            if ( ImGui::IsItemClicked() )
                Core::PanelRequests::Open( "Logs" );
            ImGui::SameLine( 0.0f, 16.0f );
        }

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

    // One toolbar button: an icon, an optional label, and an "armed" state that is drawn as a tinted fill
    // plus a 2px underline. The underline matters — a tint alone is ambiguous against a hover, and the
    // question "which mode am I in" has to be answerable from across the room.
    bool EditorLayer::ToolbarButton( const char* icon, const char* label, bool active, const char* tooltip,
                                     bool enabled )
    {
        namespace ImGui = ::ImGui;

        char text[192];
        if ( label && *label )
            std::snprintf( text, sizeof( text ), "%s  %s", icon, label );
        else
            std::snprintf( text, sizeof( text ), "%s", icon );

        const ImVec4 accent = ThemeManager::GetSelectedColor();
        ImGui::PushStyleColor( ImGuiCol_Button, active ? ImVec4( accent.x, accent.y, accent.z, 0.30f )
                                                       : ImVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 1.0f, 1.0f, 1.0f, 0.09f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 1.0f, 1.0f, 1.0f, 0.16f ) );
        ImGui::PushStyleColor( ImGuiCol_Text,
                               active ? ImGui::GetStyleColorVec4( ImGuiCol_Text ) : ThemeManager::GetIconColor() );
        if ( !enabled )
            ImGui::BeginDisabled();

        const bool clicked = ImGui::Button( text );

        if ( active )
        {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( mn.x, mx.y - 2.0f ), mx,
                                                       ImGui::GetColorU32( accent ) );
        }
        if ( !enabled )
            ImGui::EndDisabled();
        ImGui::PopStyleColor( 4 );

        if ( tooltip && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
            ImGui::SetTooltip( "%s", tooltip );
        return clicked;
    }

    void EditorLayer::ToolbarSeparator()
    {
        namespace ImGui = ::ImGui;
        ImGui::SameLine( 0.0f, 8.0f );
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float  h = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddLine( ImVec2( p.x, p.y + 3.0f ), ImVec2( p.x, p.y + h - 3.0f ),
                                             IM_COL32( 70, 70, 70, 255 ) );
        ImGui::SameLine( 0.0f, 9.0f );
    }

    void EditorLayer::DrawToolbar()
    {
        namespace ImGui = ::ImGui;
        using Gz        = ::Desert::Editor::Core::GizmoState;
        using Mode      = ::Desert::Editor::Core::ViewportMode;
        using EMode     = ::Desert::Editor::Core::EditorMode;

        // THE STRIP HAS WORK NOW.
        //
        // It used to hold two playback buttons hard against the right edge and about 900px of nothing, and
        // the comment here argued that a second row of commands was "more chrome between the menu and the
        // picture". That was true of a DUPLICATE row. What the owner approved instead is the row UE
        // actually ships: the four things that are true of the whole editor rather than of one panel —
        // what you can undo, what mode you are in, how the gizmo behaves, and whether the world is
        // running — none of which had a home. Editor MODES in particular could only be reached from a
        // combo inside the viewport's own strip, which is the one place you cannot see while looking at
        // another panel.
        //
        // Everything here drives state that already exists and already has exactly one owner: CommandHistory,
        // ViewportMode, GizmoState, Scene::GetState. No control on this bar holds a value of its own.
        const float barHeight = ImGui::GetFrameHeight() + 12.0f;

        ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.086f, 0.086f, 0.086f, 1.0f ) ); // #161616 strip
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f, 4.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 2.0f, 0.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 8.0f, 5.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 4.0f );
        ImGui::BeginChild( "##Toolbar", ImVec2( 0.0f, barHeight ), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

        const bool editMode = m_MainScene->GetState() == ::Desert::Core::Scene::SceneState::Edit;

        // ---- Left: the file/history group -------------------------------------------------------
        const bool dirty = CommandHistory::Get().Revision() != s_SavedRevision;
        if ( ToolbarButton( ICON_MDI_CONTENT_SAVE, "Save", false,
                            dirty ? "Save the scene (Ctrl+S) — there are unsaved changes"
                                  : "Save the scene (Ctrl+S)" ) )
        {
            // The SAME deferred flag the File menu sets, not a second call to Serialize: saving mid-frame
            // from a toolbar and saving from a menu must be one code path, or one of them will grow a
            // step (the revision marker, a toast) the other forgets.
            m_SaveSceneRequested = true;
        }
        ImGui::SameLine();

        const auto& undoStack = CommandHistory::Get().UndoStack();
        const auto& redoStack = CommandHistory::Get().RedoStack();
        // The tooltip NAMES the edit, which is the difference between an undo button and a dare.
        const std::string undoTip =
             undoStack.empty() ? "Nothing to undo" : "Undo " + undoStack.back()->GetLabel() + " (Ctrl+Z)";
        const std::string redoTip =
             redoStack.empty() ? "Nothing to redo" : "Redo " + redoStack.back()->GetLabel() + " (Ctrl+Shift+Z)";
        if ( ToolbarButton( ICON_MDI_UNDO, "", false, undoTip.c_str(), editMode && !undoStack.empty() ) )
            CommandHistory::Get().Undo();
        ImGui::SameLine();
        if ( ToolbarButton( ICON_MDI_REDO, "", false, redoTip.c_str(), editMode && !redoStack.empty() ) )
            CommandHistory::Get().Redo();
        ToolbarSeparator();

        // ---- Editor modes -----------------------------------------------------------------------
        // Exactly the three EditorMode values the engine HAS. The mock also drew Landscape and Paint;
        // those modes do not exist, and a button that switches to nothing is a dead setting whichever
        // picture it came from.
        const EMode mode = Mode::Get();
        if ( ToolbarButton( ICON_MDI_CURSOR_DEFAULT_OUTLINE, "Select", mode == EMode::Select,
                            "Selection and transform tools" ) )
            Mode::Set( EMode::Select );
        ImGui::SameLine();
        if ( ToolbarButton( ICON_MDI_CUBE_OUTLINE, "Modeling", mode == EMode::Modeling,
                            "Geometry tools (CubeGrid blockout)" ) )
            Mode::Set( EMode::Modeling );
        ImGui::SameLine();
        if ( ToolbarButton( ICON_MDI_GRASS, "Foliage", mode == EMode::Foliage, "Paint instanced vegetation" ) )
            Mode::Set( EMode::Foliage );
        ToolbarSeparator();

        // ---- Transform tools --------------------------------------------------------------------
        const Gz::Operation op = Gz::Get();
        if ( ToolbarButton( ICON_MDI_CURSOR_MOVE, "", op == Gz::Operation::Translate, "Translate (W)" ) )
            Gz::Set( Gz::Operation::Translate );
        ImGui::SameLine();
        if ( ToolbarButton( ICON_MDI_ROTATE_ORBIT, "", op == Gz::Operation::Rotate, "Rotate (E)" ) )
            Gz::Set( Gz::Operation::Rotate );
        ImGui::SameLine();
        if ( ToolbarButton( ICON_MDI_ARROW_EXPAND_ALL, "", op == Gz::Operation::Scale, "Scale (R)" ) )
            Gz::Set( Gz::Operation::Scale );
        ToolbarSeparator();

        // ---- The two snap values ----------------------------------------------------------------
        // Each button both REPORTS its step and opens the list that changes it, and the shared magnet
        // toggle sits at the top of both lists rather than becoming a third button: snapping is one state,
        // and two buttons for it would be two places to read a single yes/no.
        DrawSnapControl( /*rotation=*/false );
        ImGui::SameLine();
        DrawSnapControl( /*rotation=*/true );

        // ---- Centre: playback -------------------------------------------------------------------
        // Centred, deliberately. Playback is the only control here that says what the WORLD is doing
        // rather than what the editor is doing, and in the right-hand corner it read as one more tool.
        // Clamped so it never lands on the mode rail on a narrow window — it slides right instead of
        // overlapping, because a Play button under another button is worse than an off-centre one.
        {
            const float  leftEnd = ImGui::GetItemRectMax().x;
            const float  btnH    = ImGui::GetFrameHeight();
            const ImVec2 btnSize( btnH * 1.6f, btnH );
            const float  playW     = 84.0f;
            const float  groupW    = playW + ( btnSize.x + 2.0f ) * 2.0f;
            const float  windowMid = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x * 0.5f;
            const float  startX    = std::max( windowMid - groupW * 0.5f, leftEnd + 24.0f );

            ImGui::SameLine();
            ImGui::SetCursorScreenPos( ImVec2( startX, ImGui::GetCursorScreenPos().y ) );

            // Play is the bar's PRIMARY action and gets the accent; Pause is a modifier of a state that
            // is already running and stays neutral. Undifferentiated, the pair reads as two equal
            // buttons and the eye has to read the glyphs to find the one it wants.
            const ImVec4 accent = ThemeManager::GetSelectedColor();
            ImGui::PushStyleColor( ImGuiCol_Button, accent );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered,
                                   ImVec4( accent.x + 0.10f, accent.y + 0.08f, accent.z + 0.06f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive,
                                   ImVec4( accent.x * 0.8f, accent.y * 0.8f, accent.z * 0.8f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ) );
            DrawPlayButton( ImVec2( playW, btnH ) );
            ImGui::PopStyleColor( 4 );

            ImGui::SameLine();
            DrawPauseButton( btnSize );
        }

        // ---- Right: the things you leave the editor through --------------------------------------
        {
            ImGui::SameLine();
            const float rightGroupW = 330.0f;
            const float x           = std::max( ImGui::GetItemRectMax().x + 24.0f,
                                                ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - rightGroupW );
            ImGui::SetCursorScreenPos( ImVec2( x, ImGui::GetCursorScreenPos().y ) );

            if ( ToolbarButton( ICON_MDI_PACKAGE_VARIANT_CLOSED, "Package", false,
                                "Build and package the project" ) )
                Core::PanelRequests::Open( "Build Settings" );
            ImGui::SameLine();
            if ( ToolbarButton( ICON_MDI_MONITOR_DASHBOARD, "Profiler", m_ShowProfiler,
                                "Per-pass CPU and GPU timings" ) )
                m_ShowProfiler = !m_ShowProfiler;
            ImGui::SameLine();
            if ( ToolbarButton( ICON_MDI_COG, "Settings", s_ShowPreferences, "Editor preferences" ) )
                s_ShowPreferences = !s_ShowPreferences;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar( 4 );
        ImGui::PopStyleColor();
    }

    void EditorLayer::DrawSnapControl( bool rotation )
    {
        namespace ImGui = ::ImGui;
        using Gz        = ::Desert::Editor::Core::GizmoState;

        // The steps are declared once at the top of this file, because the command palette offers exactly
        // these and a second copy here is how the two lists would drift apart.

        char label[64];
        if ( rotation )
            std::snprintf( label, sizeof( label ), "%.0f\xC2\xB0", Gz::RotateSnapDegrees() );
        else if ( Gz::TranslateSnap() >= 100.0f )
            std::snprintf( label, sizeof( label ), "%.0f m", Gz::TranslateSnap() / 100.0f );
        else
            std::snprintf( label, sizeof( label ), "%.0f cm", Gz::TranslateSnap() );

        const char* icon = Gz::PersistentSnap() ? ICON_MDI_MAGNET_ON : ICON_MDI_MAGNET;
        const char* tip  = rotation ? "Angle snap — click to change the step or toggle snapping"
                                    : "Grid snap — click to change the step or toggle snapping";
        if ( ToolbarButton( rotation ? ICON_MDI_ANGLE_ACUTE : icon, label, Gz::PersistentSnap(), tip ) )
            ImGui::OpenPopup( rotation ? "##AngleSnapPopup" : "##GridSnapPopup" );

        if ( ImGui::BeginPopup( rotation ? "##AngleSnapPopup" : "##GridSnapPopup" ) )
        {
            bool snapping = Gz::PersistentSnap();
            if ( ImGui::Checkbox( "Snapping", &snapping ) )
                Gz::SetPersistentSnap( snapping );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Holding Ctrl while dragging inverts this." );
            ImGui::Separator();

            if ( rotation )
            {
                for ( const float step : kAngleSteps )
                {
                    char item[32];
                    std::snprintf( item, sizeof( item ), "%.0f\xC2\xB0", step );
                    if ( ImGui::Selectable( item, Gz::RotateSnapDegrees() == step ) )
                        Gz::SetRotateSnapDegrees( step );
                }
            }
            else
            {
                for ( const float step : kGridSteps )
                {
                    char item[32];
                    if ( step >= 100.0f )
                        std::snprintf( item, sizeof( item ), "%.0f m", step / 100.0f );
                    else
                        std::snprintf( item, sizeof( item ), "%.0f cm", step );
                    if ( ImGui::Selectable( item, Gz::TranslateSnap() == step ) )
                        Gz::SetTranslateSnap( step );
                }
            }
            ImGui::EndPopup();
        }
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

        // TextUnformatted, not Text: ImGui::Text takes a printf FORMAT, so this passed runtime-built
        // engine stats as the format string. Today GetFormattedStats() can only produce
        // "FPS: 60 | Frame: 16.6ms" and contains no '%', so nothing has gone wrong — but the day any
        // percentage is added to that line (a GPU utilisation, a budget fraction — the obvious next
        // additions) ImGui's vsnprintf reads a vararg that was never passed.
        ImGui::TextUnformatted( text.c_str() );
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
            ImGui::Checkbox( "Snap always on (Ctrl inverts)", &prefs.PersistentSnap );
            // CENTIMETRES, which is what the value has always been fed into: this control said "(m)" and
            // clamped to 0.01..100 while writing a field GizmoState reads as world units, and a world
            // unit is 1 cm. A slider whose unit disagrees with its consumer is how the shipped grid snap
            // ended up at half a centimetre (see EditorPreferences::TranslateSnap).
            //
            // Nothing is pushed anywhere afterwards: these four ARE the snap's storage and GizmoState
            // reads them, so the gizmo follows on the same frame. The block that used to copy them into
            // GizmoState is gone with the copy it fed (К6).
            ImGui::DragFloat( "Move (cm)", &prefs.TranslateSnap, 1.0f, 1.0f, 10000.0f, "%.0f" );
            ImGui::DragFloat( "Rotate (deg)", &prefs.RotateSnapDeg, 0.5f, 0.1f, 180.0f, "%.1f" );
            ImGui::DragFloat( "Scale", &prefs.ScaleSnap, 0.01f, 0.01f, 10.0f, "%.2f" );

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

        // TWENTY-ONE TOOLS, TWELVE ENTRIES. A flat alphabet-of-whatever-was-constructed-first list is a
        // list nobody reads; grouped by what the entry is FOR, the twelve that answer "where do I look at
        // the level / the content / the output" stay at the top level and the nine that belong to a
        // particular job move behind the job's own submenu. Nothing is deleted and nothing becomes
        // unreachable — see the leftover section at the end, which is empty when every panel is placed.
        //
        // AND NO DOCUMENTS. Not because this loop skips them: because m_Panels is a PanelRegistry and
        // cannot hold one. That is the whole task. Open documents are in Window -> Documents, where the
        // control is a radio and the close is an x, neither of which can be mistaken for "hide".
        // THE GROUPING IS DATA, NOT CONTROL FLOW, and that is a correction rather than a preference: a
        // submenu's body only runs while it is OPEN, so marking a panel "placed" from inside one reported
        // every panel behind a closed submenu as ungrouped. Measured — the first capture of this menu showed
        // ten panels under "NOT YET GROUPED" that are grouped. The census has to be readable without opening
        // anything, so it is stated once here and the drawing below refers to it.
        static constexpr const char* kLevelGroup[]     = { "Scene Outliner", "Collections", "Details",
                                                           "Scene Settings", "Scene Validation" };
        static constexpr const char* kContentGroup[]   = { "Assets", "Asset References", "Shader Library" };
        static constexpr const char* kOutputGroup[]    = { "Logs", "Lua Console", "History" };
        static constexpr const char* kViewportGroup[]  = { "Scene###scene" };
        // "Anim Graph" and "Particle Editor" are gone from this list because they are gone from the
        // registry this menu loops over — a name left here would draw a group entry for a panel that does
        // not exist. They are opened from the component that holds them, in Details.
        static constexpr const char* kGraphGroup[]     = { "Node Graph", "UI Editor" };
        static constexpr const char* kSequencerGroup[] = { "Sequencer", "Anim Layers" };
        static constexpr const char* kToolGroup[]      = { "Modeling", "Model from Photos", "Build Settings" };

        std::unordered_set<std::string> placed;
        for ( const auto& group :
              { std::span<const char* const>( kLevelGroup ), std::span<const char* const>( kContentGroup ),
                std::span<const char* const>( kOutputGroup ), std::span<const char* const>( kViewportGroup ),
                std::span<const char* const>( kGraphGroup ), std::span<const char* const>( kSequencerGroup ),
                std::span<const char* const>( kToolGroup ) } )
            for ( const char* name : group )
                placed.insert( name );

        auto panelItem = [&]( const char* name )
        {
            for ( auto& panel : m_Panels )
            {
                if ( panel->GetName() != name )
                    continue;

                // Same icon + stable ID as the panel title (the ###id keeps each menu entry unique/stable).
                const bool wasVisible = panel->GetVisibility();
                if ( ImGui::MenuItem( PanelDisplayTitle( panel->GetName() ).c_str(), "", &panel->GetVisibility(),
                                      true ) )
                {
                    // Ticking a contextual panel pins it open; unticking releases it back to the context.
                    if ( panel->IsContextual() )
                        panel->Pinned() = !wasVisible;
                }
                if ( panel->IsContextual() && ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "Opens itself when its context appears. Ticking it keeps it open "
                                       "even when it does not apply." );
                return;
            }
        };

        auto group = [&]( std::span<const char* const> names )
        {
            for ( const char* name : names )
                panelItem( name );
        };

        ImGui::TextDisabled( "THE LEVEL" );
        group( kLevelGroup );

        ImGui::Separator();
        ImGui::TextDisabled( "CONTENT" );
        group( kContentGroup );

        ImGui::Separator();
        ImGui::TextDisabled( "OUTPUT" );
        group( kOutputGroup );
        // The Profiler is a window this layer draws itself rather than an IPanel, so it is a bool and not a
        // registry entry — it belongs in the group all the same, because the user is choosing between it and
        // the Logs beside it, not between two implementations.
        ImGui::MenuItem( ICON_MDI_CHART_BAR "  Profiler", "", &m_ShowProfiler, true );

        ImGui::Separator();

        // The nine that moved. Each is behind the job it belongs to rather than in a flat list beside
        // "Details" — a viewport is not a panel you tick, and a timeline is somewhere you go to author a
        // clip.
        if ( ImGui::BeginMenu( ICON_MDI_MONITOR "  Viewports" ) )
        {
            group( kViewportGroup );
            ImGui::Separator();
            // Multi-scene editing: a second, independent scene in its own live viewport (own SceneRenderer)
            // so a UI scene and the game scene can be worked on side by side. Focus a viewport to make its
            // scene active — the Outliner / Details / gizmo follow it.
            if ( ImGui::MenuItem( ICON_MDI_PLUS_BOX_MULTIPLE " New Scene View" ) )
                m_AddSceneViewRequested = true; // deferred to OnUpdate (allocates GPU resources)
            if ( !m_ExtraScenes.empty() )
                ImGui::TextDisabled( "%d scene view(s) open + main", static_cast<int>( m_ExtraScenes.size() ) );
            // Closing from here does exactly what the window's x does — clear the VIEWPORT PANEL's
            // visibility — rather than tearing the scene down inside the ImGui pass. A scene view is a tool
            // panel bound to a scene, so visibility genuinely is its close signal; a document is the case
            // where that stopped being true, which is why documents have their own path.
            for ( const auto& doc : m_ExtraScenes )
            {
                const std::string item = std::string( ICON_MDI_CLOSE " Close " ) + doc->Name;
                if ( ImGui::MenuItem( item.c_str() ) && doc->Viewport )
                    doc->Viewport->GetVisibility() = false;
            }
            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( ICON_MDI_GRAPH "  Graph Editors" ) )
        {
            group( kGraphGroup );
            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( ICON_MDI_CHART_TIMELINE "  Sequencer" ) )
        {
            // Both together: a clip is authored in the timeline and its layers, and two independent ticks
            // for one place you go was two decisions where there is one.
            group( kSequencerGroup );
            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( ICON_MDI_HAMMER_WRENCH "  Tools" ) )
        {
            group( kToolGroup );
            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( ICON_MDI_EYE "  Show" ) )
        {
            if ( ImGui::MenuItem( "Perf HUD", "", &EditorPreferences::Get().ShowPerfHud, true ) )
                EditorPreferences::Save(); // persist the toggle like the rest of the user prefs
            ImGui::EndMenu();
        }

        // ANYTHING THE GROUPS ABOVE DID NOT NAME. This is empty today and is not a placeholder: a panel
        // added later and forgotten here would otherwise have no menu entry at all, which is the same
        // "you cannot get it back" the documents had. It is visible precisely so that it gets fixed.
        {
            bool anyLeftover = false;
            for ( auto& panel : m_Panels )
            {
                if ( placed.count( panel->GetName() ) != 0 )
                    continue;
                if ( !anyLeftover )
                {
                    ImGui::Separator();
                    ImGui::TextDisabled( "NOT YET GROUPED" );
                    anyLeftover = true;
                }
                panelItem( panel->GetName().c_str() );
            }
        }

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

    bool EditorLayer::SaveSceneTo( const std::string& path )
    {
        std::error_code ec;
        std::filesystem::create_directories( std::filesystem::path( path ).parent_path(), ec );
        if ( ec )
        {
            LOG_ERROR( "[Scene] Could not create the directory for '{}': {}", path, ec.message() );
            return false;
        }

        Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
        if ( const auto written =
                  Common::Utils::FileSystem::WriteContentToFileAtomic( path, serializer.SerializeToJson() );
             !written )
        {
            LOG_ERROR( "[Scene] Could not write '{}': {}", path, written.GetError() );
            return false;
        }
        return true;
    }

    bool EditorLayer::SaveOpenScene()
    {
        const auto verdict = Editor::Core::Rules::DecideAfterSceneSave(
             m_MainScene->Serialize( m_AssetManager.get() ), m_MainScene->GetSceneName() );

        if ( verdict.MarkSceneSaved )
            s_SavedRevision = CommandHistory::Get().Revision();

        if ( verdict.IsError )
        {
            LOG_ERROR( "[Scene] {}", verdict.Message );
        }
        else
        {
            LOG_INFO( "[Scene] {}", verdict.Message );
        }

        // Refresh the launcher's tile picture for this project. Only on a save that actually
        // happened — a refused save must not leave the launcher showing a world that was never
        // written. The failure is a toast and nothing more: the scene IS saved, and a launcher tile
        // without a picture is a state the launcher already draws.
        if ( verdict.MarkSceneSaved )
            if ( const auto thumbnail = WriteProjectThumbnail(); !thumbnail.IsSuccess() )
                Editor::ToastManager::Push( "The scene was saved, but the project thumbnail was not: " +
                                                 thumbnail.GetError(),
                                            Editor::ToastLevel::Warning );

        Editor::ToastManager::Push( verdict.Message,
                                    verdict.IsError ? Editor::ToastLevel::Error : Editor::ToastLevel::Success );
        return verdict.MayDiscardScene;
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
        if ( const auto inited = m_MainScene->Init(); !inited.IsSuccess() )
            LOG_ERROR( "[EditorLayer] new scene failed to initialise: {}", inited.GetError() );

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

        // ASKED BEFORE ANYTHING IS DESTROYED, and this is the call site that makes it worth asking. Below
        // this point the undo history is dropped and the open scene is cleared; a file the loader will
        // refuse - an old autosave, a scene saved by an older build - would then have cost the user the
        // scene they had and given them nothing. So an unloadable file leaves the editor exactly as it is
        // and says why, with the command that fixes the file.
        const auto contentRead = Common::Utils::FileSystem::ReadFileContent( path );
        if ( !contentRead )
        {
            LOG_ERROR( "{0}", contentRead.GetError() );
            Editor::ToastManager::Push( "Scene not loaded — the file could not be read (see the log)",
                                        Editor::ToastLevel::Error );
            return;
        }
        const std::string& content = contentRead.GetValue();
        if ( const auto loadable = Desert::Core::ParseLoadableScene( path.string(), content ); !loadable )
        {
            LOG_ERROR( "{0}", loadable.GetError() );
            Editor::ToastManager::Push( "Scene not loaded — see the log (it names the SceneMigrator command)",
                                        Editor::ToastLevel::Error );
            return;
        }

        // Wait for GPU to be idle before destroying resources mid-frame
        EngineContext::GetInstance().GetDevice()->WaitIdle();

        // The undo history refers to entities of the OLD scene — none of it applies anymore.
        CommandHistory::Get().Clear();
        s_SavedRevision = CommandHistory::Get().Revision(); // a freshly loaded scene is "clean"

        m_MainScene->Clear();

        Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
        // Cannot fire - the same text passed the same check above, before anything was torn down. It is
        // reported and NOT returned from on purpose: the scene is already cleared by this point, so the
        // rebuild below is what leaves the editor in a coherent (empty) state rather than one holding a
        // render registry for entities that no longer exist.
        if ( const auto loaded = serializer.DeserializeFromJson( content, path.string() ); !loaded )
        {
            LOG_ERROR( "{0}", loaded.GetError() );
            Editor::ToastManager::Push( "Scene failed to load — see the log", Editor::ToastLevel::Error );
        }

        if ( const auto inited = m_MainScene->Init(); !inited.IsSuccess() )
        {
            LOG_ERROR( "[EditorLayer] loaded scene failed to initialise: {}", inited.GetError() );
            Editor::ToastManager::Push( "Scene could not be initialised — see the log",
                                        Editor::ToastLevel::Error );
        }

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

    void EditorLayer::DrawWindowMenu()
    {
        namespace ImGui = ::ImGui;

        if ( !ImGui::BeginMenu( "Window" ) )
            return;

        // A SECOND MENU, BECAUSE THESE ARE A SECOND KIND OF THING. The View menu ticks tools on and off;
        // this one lists what is open and lets you go to it or close it. Putting documents back among the
        // ticks is the defect, not the layout.
        if ( m_Documents.Empty() )
        {
            ImGui::TextDisabled( "No document open" );
            ImGui::TextDisabled( "Double-click an asset in the Content Browser." );
        }
        else
        {
            ImGui::TextDisabled( "OPEN DOCUMENTS \xe2\x80\x94 %zu", m_Documents.Count() );

            // The x column is placed against the WIDEST row, measured, not against the popup's content
            // region: a menu auto-sizes to its widest item, so asking the region where the right edge is
            // gives an answer that depends on the answer. (Measured — the first capture of this menu had no
            // x on any row, because every one of them was placed past the edge it was helping to define.)
            float widestRow = 0.0f;
            for ( const auto& document : m_Documents )
            {
                const std::string measured = std::string( ICON_MDI_RADIOBOX_MARKED ) + "  " +
                                             m_SubjectEditors.Icon( document->Subject(), kUnknownDocumentIcon ) +
                                             std::string( "  " ) + DocumentDisplayName( document->GetName() );
                widestRow = std::max( widestRow, ImGui::CalcTextSize( measured.c_str() ).x );
            }

            std::vector<SubjectId> closeRequests;
            for ( const SubjectId& subject : m_Documents.MostRecentOrder() )
            {
                const ISubjectDocument* document = m_Documents.Find( subject );
                if ( !document )
                    continue;

                ImGui::PushID( static_cast<int>( std::hash<SubjectId>{}( subject ) & 0x7fffffff ) );

                // A RADIO, NOT A CHECKBOX, and the difference is the whole argument of this task written
                // in one glyph. A tick says "shown / hidden" and invites the user to untick it — which is
                // exactly what used to destroy the document. A radio says "this is the one you are in",
                // which is true, is the only thing picking a row can mean, and offers no way to un-pick.
                const bool        active = ( subject == m_FocusedDocument );
                const std::string label =
                     std::string( active ? ICON_MDI_RADIOBOX_MARKED : ICON_MDI_RADIOBOX_BLANK ) + "  " +
                     m_SubjectEditors.Icon( document->Subject(), kUnknownDocumentIcon ) + std::string( "  " ) +
                     DocumentDisplayName( document->GetName() );

                if ( ImGui::MenuItem( label.c_str() ) )
                    FocusDocument( subject );

                ImGui::SameLine( ImGui::GetCursorPosX() + widestRow + 24.0f );
                if ( ImGui::SmallButton( ICON_MDI_CLOSE ) )
                    closeRequests.push_back( subject );
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "Close this document. It is destroyed, and its renderer slot (if it "
                                       "holds one) is returned." );

                ImGui::PopID();
            }

            ImGui::Separator();
            if ( ImGui::MenuItem( ICON_MDI_CLOSE_BOX_OUTLINE "  Close All Documents" ) )
                RequestCloseAllDocuments();

            for ( const SubjectId& subject : closeRequests )
                RequestDocumentClose( subject, "closed from Window \xe2\x96\xb8 Documents" );
        }

        // NO "SAVE ALL" HERE, AND ITS ABSENCE IS DELIBERATE.
        //
        // The mock draws one. It cannot be built honestly yet: ISubjectDocument declares no Save() and no
        // IsDirty(), the editor's single dirty flag belongs to the SCENE (a CommandHistory revision), and a
        // material document writes straight into the in-memory asset as a slider moves. "Save All" would
        // therefore have to mean "rewrite every open document's file whether or not it changed", it could
        // not report how many of them needed it, and it would touch mtimes the asset hot-reload watches.
        // A per-document dirty flag with a working copy behind it is the next task; the item waits for it.

        ImGui::EndMenu();
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

        // Opening and closing scene VIEWS moved to View -> Viewports, next to the Scene panel's own toggle.
        // This menu is about scene FILES; a viewport is not one, and two menus offering the same New Scene
        // View was two places to keep in step for one action.

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
        // NOT A FILE, and it cannot be at an old version: this text came out of SerializeToJson() a moment
        // ago in this same build, which stamps the head of both version integers. It is named rather than
        // pathed so that if it ever DOES fail, the message says which of the two things called "loading a
        // scene" broke - restoring the pre-Play state is not opening a file, and reading "Play snapshot" in
        // the log is the difference between one minute of diagnosis and twenty.
        if ( const auto restored = serializer.DeserializeFromJson( m_PlaySnapshot, "<Play snapshot>" ); !restored )
        {
            LOG_ERROR( "[Scene] Play snapshot could not be restored: {0}", restored.GetError() );
            Editor::ToastManager::Push( "Play snapshot could not be restored — see the log",
                                        Editor::ToastLevel::Error );
        }
        if ( const auto inited = m_MainScene->Init(); !inited.IsSuccess() )
        {
            LOG_ERROR( "[EditorLayer] scene failed to initialise after Play: {}", inited.GetError() );
            Editor::ToastManager::Push( "Scene could not be initialised after Play — see the log",
                                        Editor::ToastLevel::Error );
        }

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
            // A failure belongs to the attempt that produced it. Without this a save that failed once
            // would keep warning about a scene the user has since saved by hand.
            m_SaveAndOpenError.clear();
        }

        ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                 ImVec2( 0.5f, 0.5f ) );

        if ( !ImGui::BeginPopupModal( "Open Scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
            return;

        const bool havePending = m_PendingOpenScene.has_value();

        ImGui::TextUnformatted( "The current scene has unsaved changes." );
        ImGui::TextDisabled( "Open %s", havePending ? SceneLabel( *m_PendingOpenScene ).c_str() : "" );

        // A failed "Save and Open" from a previous click of this same modal. It is shown INSIDE the
        // modal rather than only as a toast because the buttons below are still live: the user is about
        // to decide whether to discard this scene, and that decision changes completely once the save
        // they asked for turns out not to have happened.
        if ( !m_SaveAndOpenError.empty() )
        {
            ImGui::Separator();
            ImGui::TextColored( ThemeManager::GetErrorColor(), "%s", m_SaveAndOpenError.c_str() );
            ImGui::TextDisabled( "\"Discard\" below would throw these changes away for good." );
        }

        ImGui::Separator();

        if ( ImGui::Button( "Save and Open", ImVec2( 130, 0 ) ) )
        {
            // THE GATE THIS WHOLE TASK EXISTS FOR. LoadScene below clears the command history and calls
            // m_MainScene->Clear() — it destroys the only copy of the work the user just asked to have
            // saved. Before the save chain returned a result this ran unconditionally, so a scene that
            // failed to reach the disk was then deleted from memory, with a green "Saved" toast over it
            // and nowhere to recover from. The modal now stays open on a failed write and says so.
            if ( SaveOpenScene() )
            {
                m_SaveAndOpenError.clear();
                if ( havePending )
                    LoadScene( *m_PendingOpenScene );
                m_PendingOpenScene.reset();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                m_SaveAndOpenError = "The scene was NOT saved — see the log for the failing step. "
                                     "Nothing has been opened and nothing has been thrown away.";
            }
        }

        ImGui::SameLine();

        if ( ImGui::Button( "Discard", ImVec2( 110, 0 ) ) )
        {
            m_SaveAndOpenError.clear();
            if ( havePending )
                LoadScene( *m_PendingOpenScene );
            m_PendingOpenScene.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if ( ImGui::Button( "Cancel", ImVec2( 110, 0 ) ) )
        {
            m_SaveAndOpenError.clear();
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
        // Discarded for the same reason as Ctrl+S: File -> Save destroys nothing, and SaveOpenScene has
        // already reported the outcome and left the unsaved mark standing if the write failed.
        (void)SaveOpenScene();
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
        for ( auto& document : m_Documents )
        {
            if ( event.m_Handled )
                break;
            document->OnEvent( event );
        }
#endif
    }

    Common::BoolResultStr EditorLayer::OnDetach()
    {
        // The socket goes first, and its file with it. A leftover path is not harmless: the next editor
        // to be given it PROBES what is there, and while a dead one only costs a log line, leaving the
        // file behind on every exit would train everybody to ignore that line.
        //
        // A request still in flight is abandoned rather than answered — the frame that would have proved
        // it is never going to be drawn, and a reply promising otherwise is exactly the lie this channel
        // is built to prevent. The client sees the connection close, which is the truth.
        if ( m_ControlInFlight )
        {
            LOG_WARN( "[Control] the editor is closing with a '{}' still in flight; it is abandoned rather "
                      "than answered, because the frame that would have proved it will not be drawn.",
                      m_ControlInFlight->Group.empty() ? "request" : m_ControlInFlight->Label );
        }
        m_ControlGate.Disarm();
        m_ControlSocket.Close();

        // THE DEVICE DIED, AND THIS IS THE LAST MOMENT THE USER'S WORK EXISTS ANYWHERE.
        //
        // Not left to the autosave timer, which has three separate reasons not to have run recently: it
        // fires every AutosaveMinutes (default 5), it skips when the command revision has not moved, and
        // it runs in Edit mode only. This one runs ONCE, unconditionally, at the moment of loss.
        //
        // It writes a SEPARATE file so that a good periodic autosave is never clobbered by it. The name
        // still contains "_autosave", which is what CrashRecovery::LatestAutosave matches on, and it is
        // the newest file there, so the recovery prompt offers this one.
        //
        // IN PLAY MODE THE AUTHORED SCENE IS WHAT GETS WRITTEN — m_PlaySnapshot, the same text Stop would
        // have restored. The live scene at that instant holds runtime mutations nobody authored and nobody
        // wants back; saving those under the user's scene name would be the wrong answer wearing the right
        // filename.
        if ( Graphic::DeviceLost::IsLost() && m_MainScene )
        {
            using SceneState = ::Desert::Core::Scene::SceneState;
            Desert::Core::SceneSerializer serializer( m_MainScene.get(), m_AssetManager.get() );
            const std::string             text =
                 m_MainScene->GetState() == SceneState::Edit ? serializer.SerializeToJson() : m_PlaySnapshot;
            if ( text.empty() )
            {
                // An empty file under a recovery name is a silent wrong answer: the prompt would offer it
                // and the user would open nothing. Say so instead.
                LOG_ERROR( "[DeviceLost] nothing could be serialized to save — the scene is in {} and its "
                           "authored snapshot is empty. Your periodic autosave, if any, is untouched.",
                           m_MainScene->GetState() == SceneState::Edit ? "Edit" : "Play" );
            }
            else
            {
                std::string name = m_MainScene->GetSceneName();
                for ( auto& ch : name )
                    if ( ch == ' ' )
                        ch = '_';
                const auto      dir = Common::Constants::Path::SCENE_PATH / "Autosave";
                std::error_code ec;
                std::filesystem::create_directories( dir, ec );
                const auto path = dir / ( name + "_devicelost_autosave" +
                                          std::string( Common::Constants::Extensions::SCENE_EXTENSION ) );
                const auto written =
                     ec ? Common::MakeFormattedError( "could not create {}: {}", dir.string(), ec.message() )
                        : Common::Utils::FileSystem::WriteContentToFileAtomic( path, text );
                // BRACES ARE REQUIRED ON BOTH ARMS: the LOG_ macros are not single statements, so a
                // braceless if/else here does not compile. The autosave block above is written the same
                // way for the same reason.
                if ( written )
                {
                    LOG_INFO( "[DeviceLost] your work was saved to {} before shutting down; the next start "
                              "will offer it.",
                              path.string() );
                }
                else
                {
                    LOG_ERROR( "[DeviceLost] the emergency save FAILED: {}. The periodic autosave in {} is "
                               "the newest copy that exists.",
                               written.GetError(), dir.string() );
                }
            }
        }

        // Clean shutdown: drop the session lock so the next start doesn't think we crashed. After a device
        // loss CrashRecovery::DisarmSession refuses, on purpose — see its own comment.
        CrashRecovery::DisarmSession();

        // The launcher's tile picture, refreshed on the way out — HERE, while the device and the
        // scene's final image still exist. Everything below this point is teardown; a few lines
        // further down there is a WaitDeviceIdle and the release of exactly the GPU objects this
        // readback needs.
        //
        // Not on a headless capture run: those open scratch projects in worktrees, and the picture
        // would be of a scene nobody chose, written into a project nobody will open. Same rule, and
        // the same reason, as staying out of the recent-projects registry.
        //
        // A failure is logged and nothing else. Refusing to shut down because a picture could not
        // be written would be the tail wagging the dog.
        if ( !Editor::ShotOptions::Get().Active() )
            if ( const auto thumbnail = WriteProjectThumbnail(); !thumbnail.IsSuccess() )
                LOG_WARN( "[Project] the tile thumbnail was not written on exit: {}", thumbnail.GetError() );

        // The app loop exits right after the last PresentFinalImage, so the GPU is still chewing on that
        // frame's command buffer. Panels own GPU objects — offscreen SceneRenderers (Details preview, asset
        // thumbnails, node-graph preview), framebuffers, descriptor pools — and destroying those while that
        // buffer is in flight is what produced the "can't be called on VkPipeline/VkDescriptorPool ... that
        // is currently in use by VkCommandBuffer" validation errors on quit. Idle first, then tear down.
        Graphic::Renderer::GetInstance().WaitDeviceIdle();

        // The thumbnail service is NOT one of the panels, and the sentence above is why that matters: it
        // names "asset thumbnails" among the GPU objects this teardown covers, but m_Panels.clear() cannot
        // reach a function-static that the panels merely talk to. Left to itself the service is destroyed at
        // __cxa_finalize, long after the device is gone, and ~AssetThumbnailRenderer's WaitDeviceIdle() then
        // dereferences a null renderer API — exit 139, measured. Released here, deterministically, while
        // there is still a device to wait on. See ThumbnailService::Shutdown().
        //
        // Before the panels rather than after: a panel's own teardown must never be able to queue one last
        // preview into a service that has already let its renderer go.
        ThumbnailService::Get().Shutdown();

        // The SECOND half of the same problem, and the half the sentence above still does not cover: the
        // component widgets keep their thumbnail caches in function-statics (StaticMeshComponent.cpp,
        // SkinnedMeshComponentWidget.cpp), so those GPU images belong to no panel and no service. Cleared
        // here for the same reason and at the same moment. See ThumbnailCache::ReleaseAll().
        ThumbnailCache::ReleaseAll();

#ifdef EBABLE_IMGUI
        // Documents BEFORE tools, and both before the ImGui layer: a document owns a PreviewViewport whose
        // UIHelper holds descriptor sets, and the device has already been idled above. Explicit rather than
        // left to ~EditorLayer, which runs after the layer stack has moved on.
        (void)m_Documents.ReleaseAll();
        m_Panels.Clear();
        // Reported and not returned even though OnDetach has a channel: everything below this line still
        // has to run, and an early return would leave the extra documents and their render slots alive.
        if ( const auto detached = m_ImGuiLayer->OnDetach(); !detached.IsSuccess() )
            LOG_ERROR( "[EditorLayer] ImGui layer failed to detach: {}", detached.GetError() );
        m_ImGuiLayer.reset();
#endif

        // Extra documents in the same order CloseSceneView uses (their panels went with m_Panels above):
        // registry, then scene, then renderer. Explicit rather than left to ~EditorLayer, which runs after
        // the layer stack has moved on and would destroy renderers at an unspecified point relative to it.
        for ( auto& doc : m_ExtraScenes )
        {
            doc->Registry.reset();
            doc->Scene.reset();
            doc->Renderer.reset();
        }
        m_ExtraScenes.clear();

        return BOOLSUCCESS;
    }

} // namespace Desert::Editor
