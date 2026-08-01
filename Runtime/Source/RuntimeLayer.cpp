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

#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/Render2D/Render2D.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Properties/Texture2DProperty.hpp>
#include <Engine/UI/UICanvasRenderer2D.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Shader/ShaderService.hpp>
#include <Engine/Core/Input.hpp>

#include <algorithm>
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
        // No ImGui: the runtime presents the frame + draws UI/splash with the engine's own Render2D (set up
        // lazily on the first present, once the swapchain framebuffer exists).

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
        TriggerSplash(); // the boot scene's splash is the game's startup splash
        return BOOLSUCCESS;
    }

    Common::BoolResultStr RuntimeLayer::OnDetach()
    {
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
        TriggerSplash();
        LOG_INFO( "[Runtime] Switched scene: {}", path );
    }

    void RuntimeLayer::TriggerSplash()
    {
        const auto& s = m_Scene->GetSettings();
        if ( s.SplashDuration > 0.0f )
        {
            m_SplashSprite   = s.SplashSprite;
            m_SplashDuration = s.SplashDuration;
            m_SplashFade     = s.SplashFade;
            m_SplashTimer    = s.SplashDuration;
        }
    }

    Common::BoolResultStr RuntimeLayer::OnUpdate( const Common::Timestep& ts )
    {
        if ( m_SplashTimer > 0.0f )
            m_SplashTimer -= ts.GetMilliseconds() * 0.001f;

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

    Common::BoolResultStr RuntimeLayer::InitPresent( const std::shared_ptr<Graphic::Framebuffer>& swapFb )
    {
        auto* shaderService = Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return Common::MakeError( "InitPresent: no shader service" );
        auto blitShader = shaderService->GetByName( "SwapchainBlit" );
        if ( !blitShader )
            return Common::MakeError( "InitPresent: missing shader 'SwapchainBlit'" );

        // Fullscreen blit pipeline (vertexless: the VS synthesizes the quad), opaque, into the swapchain.
        Graphic::GraphicsPipelineSpecification spec;
        spec.DebugName         = "SwapchainBlitPipeline";
        spec.Shader            = blitShader;
        spec.Framebuffer       = swapFb;
        spec.DepthTestEnabled  = false;
        spec.DepthWriteEnabled = false;
        spec.CullMode          = Graphic::CullMode::None;
        m_BlitPipeline         = Graphic::GraphicsPipeline::Create( spec );
        if ( !m_BlitPipeline )
            return Common::MakeError( "InitPresent: failed to create blit pipeline" );
        m_BlitPipeline->Invalidate();
        m_BlitExecutor = Graphic::MaterialExecutor::Create( "SwapchainBlit", blitShader );

        m_Render2D = std::make_unique<Graphic::Render2D::Render2D>();
        if ( const auto r = m_Render2D->Init( swapFb ); !r )
            return r;

        m_PresentReady = true;
        return BOOLSUCCESS;
    }

    // The runtime's frame present — no ImGui. Opens the swapchain pass, blits the scene's final image
    // fullscreen, then draws the UI canvas + splash with the engine's Render2D batcher. (Named OnImGuiRender
    // only because that's the per-frame Layer hook the Application invokes between BeginFrame and Present.)
    Common::BoolResultStr RuntimeLayer::OnImGuiRender()
    {
        auto& renderer = Graphic::Renderer::GetInstance();
        renderer.BeginSwapChainRenderPass();

        std::string clicked;
        if ( const auto swapFb = renderer.GetCompositeFramebuffer() )
        {
            if ( !m_PresentReady )
                if ( const auto r = InitPresent( swapFb ); !r )
                    LOG_ERROR( "[Runtime] present init failed: {}", r.GetError() );

            const uint32_t width  = swapFb->GetFramebufferWidth();
            const uint32_t height = swapFb->GetFramebufferHeight();
            if ( width > 0 && height > 0 && ( width != m_LastWidth || height != m_LastHeight ) )
            {
                m_LastWidth     = width;
                m_LastHeight    = height;
                m_PendingResize = { width, height };
            }
            const float w = static_cast<float>( width );
            const float h = static_cast<float>( height );

            if ( m_PresentReady )
            {
                // 1) Present the scene: blit its final (tonemapped) image over the whole swapchain.
                if ( const auto image = m_Scene->GetFinalImage() )
                {
                    if ( auto tp = m_BlitExecutor->GetTexture2DProperty( "u_Texture" ) )
                        tp->SetImage( image.get() );
                    renderer.SubmitFullscreenQuad( m_BlitPipeline.get(), m_BlitExecutor.get() );
                }

                // 2) UI + splash via Render2D, on top.
                m_Render2D->BeginFrame( { 0.0f, 0.0f, w, h } );
                auto& dl = m_Render2D->GetDrawList();

                glm::mat4        vp( 1.0f );
                const glm::mat4* vpPtr = nullptr;
                if ( auto cam = m_Scene->GetMainCamera().lock() )
                {
                    vp    = cam->GetProjectionMatrix() * cam->GetViewMatrix();
                    vpPtr = &vp;
                }

                // Fullscreen: window mouse px == framebuffer px. MouseReleased is the down->up edge.
                const auto [mx, my] = Input::Mouse::Get().GetMousePosition();
                const bool  down    = Input::Mouse::Get().IsMouseButtonPressed( Common::MouseButton::Left );
                UI::UIInput input;
                input.MousePx       = { mx, my };
                input.MouseDown     = down;
                input.MouseReleased = m_PrevMouseDown && !down;
                m_PrevMouseDown     = down;

                UI::RenderCanvas2D( m_Scene->GetRegistry(), dl, UI::Rect{ 0.0f, 0.0f, w, h }, vpPtr, &input,
                                    &clicked );

                if ( m_SplashTimer > 0.0f )
                {
                    const float elapsed = m_SplashDuration - m_SplashTimer;
                    float       a       = 1.0f;
                    if ( m_SplashFade > 0.0f )
                    {
                        if ( elapsed < m_SplashFade )
                            a = elapsed / m_SplashFade; // fade in
                        else if ( m_SplashTimer < m_SplashFade )
                            a = m_SplashTimer / m_SplashFade; // fade out
                    }
                    a = std::clamp( a, 0.0f, 1.0f );

                    dl.AddRectFilled( { 0.0f, 0.0f }, { w, h }, glm::vec4( 0.0f, 0.0f, 0.0f, a ) ); // fade
                    if ( auto* tex = Runtime::ResourceRegistry::GetTextureService()->Get( m_SplashSprite ) )
                    {
                        auto* img = static_cast<Graphic::Image2D*>(
                             Runtime::ResourceRegistry::GetImageService()->Resolve( tex->GetImageHandle() ) );
                        if ( img && img->GetWidth() > 0 && img->GetHeight() > 0 )
                        {
                            const float iw  = static_cast<float>( img->GetWidth() );
                            const float ih  = static_cast<float>( img->GetHeight() );
                            const float fit = std::min( w / iw, h / ih );
                            const float sw = iw * fit, sh = ih * fit;
                            const float cx = w * 0.5f, cy = h * 0.5f;
                            dl.AddImage( img, { cx - sw * 0.5f, cy - sh * 0.5f },
                                         { cx + sw * 0.5f, cy + sh * 0.5f }, { 0.0f, 0.0f }, { 1.0f, 1.0f },
                                         glm::vec4( 1.0f, 1.0f, 1.0f, a ) );
                        }
                    }
                }

                m_Render2D->Flush();
            }
        }

        renderer.EndRenderPass();

        // Dispatch the clicked button's action AFTER the pass (scene switch queued for next OnUpdate; quit /
        // URL are process-level). Same encoding the UI walker produces.
        if ( !clicked.empty() )
        {
            constexpr std::string_view kScene = "scene:";
            constexpr std::string_view kUrl   = "url:";
            if ( clicked.rfind( kScene, 0 ) == 0 )
            {
                m_PendingSceneLoad = clicked.substr( kScene.size() );
            }
            else if ( clicked == "quit" )
            {
                LOG_INFO( "[Runtime] UI quit requested" );
                std::exit( 0 );
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

        return BOOLSUCCESS;
    }

    void RuntimeLayer::OnEvent( Common::Event& )
    {
    }
} // namespace Desert::Player
