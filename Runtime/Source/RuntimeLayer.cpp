#include "RuntimeLayer.hpp"

#include <Engine/Core/Scene.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Core/Serialize/SceneSerializer.hpp>
#include <Engine/Project/ProjectContext.hpp>
#include <Engine/UI/UICanvasRenderer.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/UICacheTexture.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetPreloader.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Engine/ECS/System/MeshECSSystem.hpp>
#include <Engine/ECS/System/TextECSSystem.hpp>
#include <Engine/ECS/System/SkyboxECSSystem.hpp>
#include <Engine/ECS/System/TerrainECSSystem.hpp>
#include <Engine/ECS/System/PointLightSystem.hpp>
#include <Engine/ECS/System/SpotLightSystem.hpp>
#include <Engine/ECS/System/AnimationECSSystem.hpp>
#include <Engine/ECS/System/AttachmentSystem.hpp>
#include <Engine/ECS/System/ScriptSystem.hpp>
#include <Engine/ECS/System/PhysicsECSSystem.hpp>
#include <Engine/ECS/System/LocomotionSystem.hpp>
#include <Engine/ECS/System/AudioECSSystem.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Logger.hpp>

#include <ImGui/imgui.h>

#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace Desert::Player
{
    RuntimeLayer::RuntimeLayer( std::string scenePathOverride )
         : Common::Layer( "RuntimeLayer" ), m_ScenePathOverride( std::move( scenePathOverride ) )
    {
        m_AssetManager     = std::make_shared<Assets::AssetManager>();
        m_AssetPreloader   = std::make_unique<Assets::AssetPreloader>( m_AssetManager );
        m_AnimationLibrary = std::make_unique<Animation::AnimationLibrary>( m_AssetManager.get() );
        m_SceneRenderer    = std::make_unique<Graphic::SceneRenderer>();
        m_Scene            = std::make_shared<Core::Scene>( "Game", m_SceneRenderer.get() );
    }

    RuntimeLayer::~RuntimeLayer() = default;

    Common::BoolResultStr RuntimeLayer::OnAttach()
    {
        // Minimal ImGui setup — the runtime uses it ONLY as the fullscreen presenter of the rendered frame.
        ::ImGui::CreateContext();
        m_ImGuiLayer = ::Desert::ImGui::ImGuiLayer::Create();
        m_ImGuiLayer->OnAttach();

        m_UITextureCache = Graphic::UICacheTexture::Create();

        // The runtime does NOT cook: it plays what the editor cooked. Assets load from the project's
        // Cooked/ tree (missing cooked content = open the project in the editor once).
        m_AssetPreloader->PreloadShaders(); // MUST precede the render systems (ctors resolve shaders)
        m_AssetPreloader->PreloadMeshes();
        m_AssetPreloader->PreloadSkyboxes();

        // Same system set + order as the editor's Play mode.
        m_Scene->AddSystem<ECS::MeshECSSystem>();
        m_Scene->AddSystem<ECS::TextECSSystem>();
        m_Scene->AddSystem<ECS::SkyboxECSSystem>();
        m_Scene->AddSystem<ECS::TerrainECSSystem>();
        m_Scene->AddSystem<ECS::PointLightECSSystem>();
        m_Scene->AddSystem<ECS::SpotLightECSSystem>();
        m_Scene->AddSystem<ECS::AnimationECSSystem>( m_AnimationLibrary.get() );
        m_Scene->AddSystem<ECS::AttachmentSystem>( m_Scene.get() );
        m_Scene->AddSystem<ECS::ScriptSystem>( m_Scene.get(), m_AssetManager.get() );
        m_Scene->AddSystem<ECS::PhysicsECSSystem>( m_Scene.get() );
        m_Scene->AddSystem<ECS::LocomotionSystem>( m_Scene.get() );
        m_Scene->AddSystem<ECS::AudioECSSystem>( m_Scene.get() );

        if ( const auto init = m_Scene->Init(); !init )
            return init;

        // Scene: --scene override, else the project's default scene.
        std::string scenePath = m_ScenePathOverride;
        if ( scenePath.empty() )
            scenePath = Project::ProjectContext::DefaultScenePath();

        if ( !scenePath.empty() && Common::Utils::FileSystem::Exists( scenePath ) ) // VFS-aware
        {
            Core::SceneSerializer serializer( m_Scene.get(), m_AssetManager.get() );
            serializer.DeserializeFromJson( Common::Utils::FileSystem::ReadFileContent( scenePath ) );
            if ( const auto init = m_Scene->Init(); !init )
                return init;
            LOG_INFO( "[Runtime] Scene loaded: {}", scenePath );
        }
        else
        {
            LOG_WARN( "[Runtime] No scene to load ('{}') — starting empty. Set DefaultScene in the "
                      ".deproj or pass --scene <path>.",
                      scenePath );
        }

        // Straight into gameplay: scripts tick, physics runs, the main CameraComponent drives the view.
        m_Scene->SetState( Core::Scene::SceneState::Play );
        return BOOLSUCCESS;
    }

    Common::BoolResultStr RuntimeLayer::OnDetach()
    {
        if ( m_ImGuiLayer )
        {
            m_ImGuiLayer->OnDetach();
            m_ImGuiLayer.reset();
        }
        return BOOLSUCCESS;
    }

    void RuntimeLayer::LoadSceneInternal( const std::string& path )
    {
        if ( !Common::Utils::FileSystem::Exists( path ) ) // VFS-aware
        {
            LOG_WARN( "[Runtime] Scene switch target not found: '{}' (a button's OnClickMessage is "
                      "'scene:<path>' — the path must resolve in the cooked project)",
                      path );
            return;
        }

        EngineContext::GetInstance().GetDevice()->WaitIdle(); // scene teardown frees GPU resources
        m_Scene->Clear();                                     // keeps the gameplay systems, drops the entities

        Core::SceneSerializer serializer( m_Scene.get(), m_AssetManager.get() );
        serializer.DeserializeFromJson( Common::Utils::FileSystem::ReadFileContent( path ) );
        if ( const auto init = m_Scene->Init(); !init )
        {
            LOG_ERROR( "[Runtime] Scene switch init failed: {}", init.GetError() );
            return;
        }
        m_Scene->SetState( Core::Scene::SceneState::Play );
        LOG_INFO( "[Runtime] Switched scene: {}", path );
    }

    Common::BoolResultStr RuntimeLayer::OnUpdate( const Common::Timestep& ts )
    {
        // A UI button requested a scene switch last frame: apply it here, between frames, before any
        // recording starts (Clear() destroys GPU resources — same rule as the resize below).
        if ( m_PendingSceneLoad )
        {
            const std::string path = *m_PendingSceneLoad;
            m_PendingSceneLoad.reset();
            LoadSceneInternal( path );
        }

        // Window-size changes resize the scene target here — before any recording starts (destroying
        // framebuffers mid-frame is a device loss).
        if ( m_PendingResize )
        {
            m_Scene->Resize( m_PendingResize->first, m_PendingResize->second );
            if ( auto cam = m_Scene->GetMainCamera().lock() )
                cam->UpdateProjectionMatrix( static_cast<float>( m_PendingResize->first ),
                                             static_cast<float>( m_PendingResize->second ) );
            m_PendingResize.reset();
        }

        // Same safe-point garbage collection as the editor (scripts can invalidate materials live).
        if ( auto* materialService = ::Desert::Runtime::ResourceRegistry::GetMaterialService() )
            materialService->CollectGarbage();

        if ( const auto begin = m_Scene->BeginScene(); !begin )
            return Common::MakeError( begin.GetError() );

        m_Scene->OnUpdate( ts );

        if ( const auto end = m_Scene->EndScene(); !end )
            return Common::MakeError( end.GetError() );

        return BOOLSUCCESS;
    }

    Common::BoolResultStr RuntimeLayer::OnImGuiRender()
    {
        m_ImGuiLayer->Begin();

        // One chrome-less fullscreen window whose whole content is the scene's final image.
        const ImGuiViewport* viewport = ::ImGui::GetMainViewport();
        ::ImGui::SetNextWindowPos( viewport->Pos );
        ::ImGui::SetNextWindowSize( viewport->Size );
        ::ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
        ::ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
        ::ImGui::Begin( "##game", nullptr,
                        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground );

        const uint32_t width  = static_cast<uint32_t>( viewport->Size.x );
        const uint32_t height = static_cast<uint32_t>( viewport->Size.y );
        if ( width > 0 && height > 0 && ( width != m_LastWidth || height != m_LastHeight ) )
        {
            m_LastWidth     = width;
            m_LastHeight    = height;
            m_PendingResize = { width, height };
        }

        if ( const auto image = m_Scene->GetFinalImage() )
        {
            const auto* id = m_UITextureCache->AddTextureCache( image );
            ::ImGui::Image( (ImTextureID)id, viewport->Size );
        }

        // In-game UI overlay: the scene's UICanvas drawn on top of the frame with the SAME renderer the
        // editor previews (Engine/UI/UICanvasRenderer). Buttons are live — a click whose OnClickMessage is
        // "scene:<path>" queues a scene switch (applied next OnUpdate). Other messages are gameplay events a
        // ScriptSystem can consume later.
        std::string              clicked;
        const UI::SpriteResolver sprites = [this]( const std::shared_ptr<Graphic::Image2D>& img )
        { return m_UITextureCache ? m_UITextureCache->AddTextureCache( img ) : nullptr; };
        UI::RenderCanvas( m_Scene->GetRegistry(), ::ImGui::GetWindowDrawList(),
                          UI::Rect{ viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y },
                          /*interactive=*/true, &clicked, sprites );
        if ( !clicked.empty() )
        {
            // Dispatch the button's Click Action (encoded by UICanvasRenderer): scene switch / quit / open a
            // URL / a plain gameplay message a ScriptSystem can consume.
            constexpr std::string_view kScene = "scene:";
            constexpr std::string_view kUrl   = "url:";
            if ( clicked.rfind( kScene, 0 ) == 0 )
            {
                m_PendingSceneLoad = clicked.substr( kScene.size() );
            }
            else if ( clicked == "quit" )
            {
                LOG_INFO( "[Runtime] UI quit requested" );
                std::exit( 0 ); // standalone player: a game "Quit to desktop" button
            }
            else if ( clicked.rfind( kUrl, 0 ) == 0 )
            {
                const std::string url = clicked.substr( kUrl.size() );
#if defined( _WIN32 )
                const std::string cmd = "start \"\" \"" + url + "\"";
#elif defined( __APPLE__ )
                const std::string cmd = "open \"" + url + "\"";
#else
                const std::string cmd = "xdg-open \"" + url + "\"";
#endif
                if ( std::system( cmd.c_str() ) != 0 )
                    LOG_WARN( "[Runtime] OpenURL failed: {}", url );
            }
            else
            {
                LOG_INFO( "[Runtime] UI message: '{}' (consume it in a ScriptSystem)", clicked );
            }
        }

        ::ImGui::End();
        ::ImGui::PopStyleVar( 2 );

        m_ImGuiLayer->End();
        return BOOLSUCCESS;
    }

    void RuntimeLayer::OnEvent( Common::Event& )
    {
    }
} // namespace Desert::Player
