#include "AssetThumbnailRenderer.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/System/MeshECSSystem.hpp>
#include <Engine/ECS/System/SkyboxECSSystem.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Common/Core/Logger.hpp>

#include <algorithm>
#include <filesystem>
#include <cmath>

// STB_IMAGE_WRITE_IMPLEMENTATION is already compiled into Desert.lib (stb_image.obj); just declare here.
#include <stb_image/stb_image_write.h>

namespace Desert::Editor
{
    AssetThumbnailRenderer::~AssetThumbnailRenderer()
    {
        if ( !m_Inited )
            return;

        Graphic::Renderer::GetInstance().WaitDeviceIdle();
        m_Scene.reset();
        m_Renderer.reset();
    }

    void AssetThumbnailRenderer::EnsureInit()
    {
        if ( m_Inited )
            return;

        m_Renderer = std::make_unique<Graphic::SceneRenderer>();
        m_Scene    = std::make_shared<::Desert::Core::Scene>( "ThumbnailPreview", m_Renderer.get() );
        m_Scene->Init();

        // Clean preview: no editor ground grid / selection outline / shadows bleeding into the thumbnail.
        // Keep AA on (FXAA) for smoother edges; supersampling (render 2x, downscale) adds the rest.
        auto& settings         = m_Scene->GetSettings();
        settings.ShowGrid      = false;
        settings.EnableShadows = false;
        settings.EnableBloom   = false;
        settings.AA            = ::Desert::Core::AntiAliasingMode::FXAA;

        // Selection outline is an editor-preference now (no longer a scene setting); force it off on this
        // preview renderer so it never bleeds into a thumbnail (the main editor loop pushes it every frame,
        // but this offscreen renderer is never fed, so disable it explicitly).
        m_Renderer->SetOutlineSettings( glm::vec3( 0.0f ), 0.0f, 0.0f, false );

        // NOTE: the runtime Core::Camera is a fixed orbit camera (input-driven, ignores the ECS transform).
        // It sits at ~(-4.33, 6.12, -4.33) looking at the origin (distance ~8.66). We can't reposition it,
        // so thumbnails are framed by SCALING the target object at the origin to fit that fixed view.
        m_Camera = m_Scene->CreateNewEntity( "ThumbCam" );
        m_Camera.AddComponent<ECS::CameraComponent>().Data.IsMainCamera = true;

        // Key light pointing toward the camera-facing hemisphere (DirectionLight stores the *travel*
        // direction in Translation; the shader lights along -Direction). From above + the camera's side.
        auto  light           = m_Scene->CreateNewEntity( "ThumbLight" );
        auto& lightC          = light.AddComponent<ECS::DirectionLightComponent>();
        lightC.Data.Intensity = 3.5f;
        lightC.Data.Color     = { 1.0f, 0.97f, 0.92f }; // warm key
        light.GetComponent<ECS::TransformComponent>().Translation = { 2.0f, -6.0f, 5.0f };

        m_Target = m_Scene->CreateNewEntity( "ThumbTarget" );
        m_Target.AddComponent<ECS::StaticMeshComponent>();

        m_Scene->AddSystem<ECS::MeshECSSystem>();
        m_Scene->AddSystem<ECS::SkyboxECSSystem>();

        // Procedural sky entity (drawn by SkyboxECSSystem) — gives a real backdrop gradient. ALSO call the
        // direct SceneRenderer::SetProceduralSky below so the sky is enabled from frame 0 (the ECS command
        // path alone proved insufficient in this minimal scene). Sun dir = the ThumbLight.
        // Through the engine's ONE negation, not a second hand-written one (ECS::Rules::AtmosphereSunDirection):
        // the light's Translation is the direction it TRAVELS, the sky wants the direction toward the sun.
        const glm::vec3 sunDir = ECS::Rules::AtmosphereSunDirection( glm::vec3( 2.0f, -6.0f, 5.0f ) );

        // IMPORTANT: the fixed preview camera sits ABOVE the object (y=6.12) looking DOWN, so the backdrop
        // samples the sky's LOWER (ground) hemisphere — NOT the zenith. So GroundColor is what's actually
        // visible behind the object; we make it a soft sky-blue (a dark ground read as muddy gray after
        // tonemap, which looked like "no sky"). The whole dome is a cohesive light blue so any view angle
        // gives a pleasant backdrop.
        auto  skyEnt             = m_Scene->CreateNewEntity( "ThumbSky" );
        auto& skyC               = skyEnt.AddComponent<ECS::SkyAtmosphereComponent>();
        skyC.Data.ZenithColor    = { 0.26f, 0.46f, 0.78f };
        skyC.Data.HorizonColor   = { 0.62f, 0.73f, 0.87f };
        skyC.Data.GroundColor    = { 0.45f, 0.56f, 0.72f }; // visible behind the object (camera looks down)
        skyC.Data.SunColor       = { 1.00f, 0.95f, 0.85f };
        skyC.Data.SkyBrightness  = 1.15f;
        skyC.Data.HorizonFalloff = 0.5f;
        skyC.Data.SunGlow        = 0.8f;
        skyC.Data.StarIntensity  = 0.0f;
        skyC.Data.SunIntensity   = 16.0f;
        skyC.RequestBake         = true;

        // The SAME values via the direct call (enabled from frame 0) — through the one packing helper, so
        // this route and the ECS route cannot describe two different skies. The eight literals above used
        // to be typed a second time here, which is how a field added to the component reached the viewport
        // and not the thumbnails.
        // Default SunLightFx: a thumbnail has no sun light entity to read shafts from, and streaks in a
        // 128px preview would be noise anyway.
        m_Renderer->SetProceduralSky( true, sunDir, /*bakeNow=*/true, Graphic::MakeSkySettings( skyC.Data ),
                                      Graphic::SunLightFx{} );

        // Resize ONCE here (after the camera exists) so the camera projection becomes square. We render at
        // kRenderSize (2x the output) and downscale on write = supersampled anti-aliasing. Resize recreates
        // framebuffers + idles the GPU, so we never call it per render.
        m_Scene->Resize( kRenderSize, kRenderSize );

        m_Inited = true;
    }

    void AssetThumbnailRenderer::FitTarget( const glm::vec3& center, float worldSize )
    {
        // Recenter the target at the origin and scale it so its largest extent spans ~kFitSpan world units,
        // which fills the fixed 45-deg camera (distance ~8.66) with a little margin.
        constexpr float kFitSpan = 4.0f;
        const float     scale    = worldSize > 1e-4f ? kFitSpan / worldSize : kFitSpan;

        auto& tc       = m_Target.GetComponent<ECS::TransformComponent>();
        tc.Scale       = glm::vec3( scale );
        tc.Rotation    = glm::vec3( 0.0f );
        tc.Translation = -center * scale; // bring the object's center to the origin
    }

    void AssetThumbnailRenderer::RecordRender()
    {
        // Records the scene render into the CURRENT editor frame's command buffer. It is NOT submitted yet
        // (that happens when the editor's frame ends), so the readback must wait until a later frame -
        // see Collect().
        m_Scene->BeginScene();
        m_Scene->OnUpdate( Common::Timestep( 0.016f ) );
        m_Scene->EndScene();
    }

    void AssetThumbnailRenderer::RequestMaterial( const Assets::AssetHandle& materialHandle,
                                                  const std::string& outPng, bool flatPreview )
    {
        if ( static_cast<uint64_t>( materialHandle ) == 0 || m_Phase != 0 )
            return;
        m_PendingHandle      = materialHandle;
        m_PendingPng         = outPng;
        m_PendingIsMesh      = false;
        m_PendingFlatPreview = flatPreview;
        // Render for several frames before reading back: the first renders after init aren't "warm" yet
        // (GPU mesh buffers + per-frame uniform-buffer ring slots need a few frames to fully populate), so an
        // early readback returns an empty frame. Capture happens on the last count (reads the prior, warm
        // frame's already-submitted render).
        m_Phase = kRenderFrames;
    }

    void AssetThumbnailRenderer::RequestMesh( const Assets::AssetHandle& meshHandle, const std::string& outPng,
                                              const Assets::AssetHandle& material )
    {
        if ( static_cast<uint64_t>( meshHandle ) == 0 || m_Phase != 0 )
            return;
        m_PendingHandle   = meshHandle;
        m_PendingMaterial = material;
        m_PendingPng      = outPng;
        m_PendingIsMesh   = true;
        m_Phase           = kRenderFrames;
    }

    void AssetThumbnailRenderer::Tick()
    {
        if ( m_Phase == 0 )
            return;
        EnsureInit();

        auto& smc = m_Target.GetComponent<ECS::StaticMeshComponent>();

        // NOTE ON WHAT IS NOT HERE. This used to add an ECS::MaterialComponent to the preview target and
        // write MaterialComponent::ShaderName into it, to serve a RequestShader() entry point that nothing
        // in the editor ever called. That is the shader-OVERRIDE route, and it is the exact route behind
        // the Stage-1 defect where a thumbnail showed a correct material while the scene showed black —
        // the two paths resolve a material differently, so a preview taken on one proves nothing about the
        // other (Docs/MaterialEditor/STAGE1_END_TO_END.md). Every capture below now goes through
        // MaterialSlots, the per-slot route the scene itself uses.

        if ( m_PendingIsMesh )
        {
            // Asset mesh, auto-framed by its bounds. Apply the mesh's linked (sidecar) material to every slot
            // if one was provided, so the preview shows the real look instead of a flat default gray.
            smc.RuntimeMesh.reset();
            smc.Primitive.reset();
            smc.RuntimeMaterialInstances.clear();
            smc.MeshHandle = m_PendingHandle;

            glm::vec3 center( 0.0f );
            float     extent = 1.0f;
            if ( auto* mesh = Runtime::ResourceRegistry::GetMeshService()->Get( m_PendingHandle ) )
            {
                // Union the submesh AABBs in MESH space (apply each submesh transform to its 8 corners) so
                // meshes with per-submesh transforms frame correctly (otherwise some come out huge / off-screen).
                glm::vec3 mn( 1e9f ), mx( -1e9f );
                for ( const auto& sm : mesh->GetSubmeshes() )
                {
                    const glm::vec3 lo = sm.BoundingBox.Min, hi = sm.BoundingBox.Max;
                    for ( int corner = 0; corner < 8; ++corner )
                    {
                        const glm::vec3 p( ( corner & 1 ) ? hi.x : lo.x, ( corner & 2 ) ? hi.y : lo.y,
                                           ( corner & 4 ) ? hi.z : lo.z );
                        const glm::vec3 w = glm::vec3( sm.Transform * glm::vec4( p, 1.0f ) );
                        mn               = glm::min( mn, w );
                        mx               = glm::max( mx, w );
                    }
                }
                if ( mx.x >= mn.x )
                {
                    center               = ( mn + mx ) * 0.5f;
                    const glm::vec3 size = mx - mn;
                    extent               = std::max( size.x, std::max( size.y, size.z ) );
                }

                // Slot count = submesh count; fill with the linked material (or leave default if none).
                if ( static_cast<uint64_t>( m_PendingMaterial ) != 0 )
                    smc.MaterialSlots.assign( std::max<size_t>( 1, mesh->GetSubmeshes().size() ),
                                              m_PendingMaterial );
                else
                    smc.MaterialSlots.clear();
            }
            else
            {
                smc.MaterialSlots.clear();
            }
            FitTarget( center, extent );
        }
        else
        {
            // Material preview. Geometry is built once and reused; clearing the runtime instances forces a
            // rebuild against the current material handle. Foliage/cutout materials (a grass-card atlas) wrap
            // and garble on a sphere, so those preview on a flat PLANE turned to face the fixed camera.
            smc.MeshHandle    = Assets::AssetHandle( static_cast<uint64_t>( 0 ) );
            smc.Primitive     = m_PendingFlatPreview ? Geometry::PrimitiveType::Plane
                                                     : Geometry::PrimitiveType::Sphere;
            smc.MaterialSlots = { m_PendingHandle };
            smc.RuntimeMaterialInstances.clear();
            smc.RuntimeMesh.reset(); // drop any previously-built primitive so the type change rebuilds
            FitTarget( glm::vec3( 0.0f ), 1.0f );

            if ( m_PendingFlatPreview )
            {
                // The plane's default normal is +Z; yaw it about Y so the front faces the fixed orbit camera
                // (kept upright — a grass card grows along +Y). Camera sits in the -X/-Z quadrant looking at
                // the origin, so yaw = atan2(camX, camZ).
                glm::vec3 camPos( -4.33f, 6.12f, -4.33f );
                if ( auto cam = m_Scene->GetMainCamera().lock() )
                    camPos = cam->GetPosition();
                m_Target.GetComponent<ECS::TransformComponent>().Rotation =
                     glm::vec3( 0.0f, std::atan2( camPos.x, camPos.z ), 0.0f );
            }
        }

        // Render this frame (recorded into the editor's in-flight frame, submitted at frame end).
        RecordRender();

        if ( m_Phase > 1 )
        {
            // Still warming up; capture on the last count.
            --m_Phase;
            return;
        }

        // Final count: the PREVIOUS (warm) frame's render is submitted + (after this wait) finished, so the
        // framebuffer readback returns it. (This frame's render isn't submitted yet, so it doesn't interfere.)
        Graphic::Renderer::GetInstance().WaitDeviceIdle();

        if ( auto finalImage = m_Scene->GetFinalImage() )
        {
            std::vector<uint8_t> src = finalImage->ReadPixelsRGBA8();
            if ( src.size() == static_cast<size_t>( kRenderSize ) * kRenderSize * 4 )
            {
                // Supersample downscale kRenderSize -> kSize via NxN box filter (clean anti-aliased edges).
                // Supersample downscale kRenderSize -> kSize (NxN box filter). The framebuffer readback is
                // already upright, so write rows in order and do NOT stbi-flip (a single flip — which used to
                // be on — made asymmetric meshes like the statue come out head-down; symmetric previews hid it).
                constexpr uint32_t   F = kRenderSize / kSize;
                std::vector<uint8_t> out( static_cast<size_t>( kSize ) * kSize * 4 );
                for ( uint32_t y = 0; y < kSize; ++y )
                    for ( uint32_t x = 0; x < kSize; ++x )
                        for ( uint32_t c = 0; c < 4; ++c )
                        {
                            uint32_t sum = 0;
                            for ( uint32_t sy = 0; sy < F; ++sy )
                                for ( uint32_t sx = 0; sx < F; ++sx )
                                    sum += src[( ( ( y * F + sy ) * kRenderSize + ( x * F + sx ) ) * 4 ) + c];
                            out[( ( y * kSize + x ) * 4 ) + c] = static_cast<uint8_t>( sum / ( F * F ) );
                        }

                std::error_code ec;
                std::filesystem::create_directories( std::filesystem::path( m_PendingPng ).parent_path(), ec );
                stbi_flip_vertically_on_write( 0 ); // readback is already upright — no flip
                stbi_write_png( m_PendingPng.c_str(), kSize, kSize, 4, out.data(), kSize * 4 );
            }
        }

        // DEBUG: when true, never finish — keep re-rendering THIS thumbnail every frame so its passes
        // (SkyboxPass etc.) appear in every editor frame and can be grabbed with RenderDoc (F12). The
        // preview render is recorded into the editor's in-flight frame, so the capture includes it.
        // TEMPORARY — set back to false (or delete) after the capture.
        static constexpr bool kDebugLoopForCapture = false;
        m_Phase = kDebugLoopForCapture ? kRenderFrames : 0;
    }
} // namespace Desert::Editor
