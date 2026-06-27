#include "AssetThumbnailRenderer.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/System/MeshECSSystem.hpp>
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
        settings.EnableOutline = false;
        settings.EnableShadows = false;
        settings.EnableBloom   = false;
        settings.AA            = ::Desert::Core::AntiAliasingMode::FXAA;

        // NOTE: the runtime Core::Camera is a fixed orbit camera (input-driven, ignores the ECS transform).
        // It sits at ~(-4.33, 6.12, -4.33) looking at the origin (distance ~8.66). We can't reposition it,
        // so thumbnails are framed by SCALING the target object at the origin to fit that fixed view.
        m_Camera = m_Scene->CreateNewEntity( "ThumbCam" );
        m_Camera.AddComponent<ECS::CameraComponent>().Data.IsMainCamera = true;

        // Key light pointing toward the camera-facing hemisphere (DirectionLight stores the *travel*
        // direction in Translation; the shader lights along -Direction). From above + the camera's side.
        auto  light           = m_Scene->CreateNewEntity( "ThumbLight" );
        auto& lightC          = light.AddComponent<ECS::DirectionLightComponent>();
        lightC.Data.Intensity = 3.0f;
        light.GetComponent<ECS::TransformComponent>().Translation = { 2.0f, -6.0f, 5.0f };

        m_Target = m_Scene->CreateNewEntity( "ThumbTarget" );
        m_Target.AddComponent<ECS::StaticMeshComponent>();

        m_Scene->AddSystem<ECS::MeshECSSystem>();

        // Procedural sky background (standard params) — also bakes the sky IBL so the preview spheres get
        // real ambient + reflections (much less flat). Sun direction matched to the key light so the bright
        // sky aligns with the lit side. bakeNow=true → the first warm-up render bakes the environment.
        const glm::vec3 sunDir = -glm::normalize( glm::vec3( 2.0f, -6.0f, 5.0f ) ); // toward the sun
        m_Renderer->SetProceduralSky( true, sunDir, 22.0f, 0.02f, true, Graphic::CloudSettings{} );

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
                                                  const std::string&         outPng )
    {
        if ( static_cast<uint64_t>( materialHandle ) == 0 || m_Phase != 0 )
            return;
        m_PendingHandle = materialHandle;
        m_PendingPng    = outPng;
        m_PendingIsMesh = false;
        // Render for several frames before reading back: the first renders after init aren't "warm" yet
        // (GPU mesh buffers + per-frame uniform-buffer ring slots need a few frames to fully populate), so an
        // early readback returns an empty frame. Capture happens on the last count (reads the prior, warm
        // frame's already-submitted render).
        m_Phase = kRenderFrames;
    }

    void AssetThumbnailRenderer::RequestMesh( const Assets::AssetHandle& meshHandle, const std::string& outPng )
    {
        if ( static_cast<uint64_t>( meshHandle ) == 0 || m_Phase != 0 )
            return;
        m_PendingHandle = meshHandle;
        m_PendingPng    = outPng;
        m_PendingIsMesh = true;
        m_Phase         = kRenderFrames;
    }

    void AssetThumbnailRenderer::Tick()
    {
        if ( m_Phase == 0 )
            return;
        EnsureInit();

        auto& smc = m_Target.GetComponent<ECS::StaticMeshComponent>();
        if ( m_PendingIsMesh )
        {
            // Asset mesh, default material(s), auto-framed by its bounds.
            smc.RuntimeMesh.reset();
            smc.Primitive.reset();
            smc.MaterialSlots.clear();
            smc.RuntimeMaterialInstances.clear();
            smc.MeshHandle = m_PendingHandle;

            glm::vec3 center( 0.0f );
            float     extent = 1.0f;
            if ( auto* mesh = Runtime::ResourceRegistry::GetMeshService()->Get( m_PendingHandle ) )
            {
                glm::vec3 mn( 1e9f ), mx( -1e9f );
                for ( const auto& sm : mesh->GetSubmeshes() )
                {
                    mn = glm::min( mn, sm.BoundingBox.Min );
                    mx = glm::max( mx, sm.BoundingBox.Max );
                }
                if ( mx.x >= mn.x )
                {
                    center               = ( mn + mx ) * 0.5f;
                    const glm::vec3 size = mx - mn;
                    extent               = std::max( size.x, std::max( size.y, size.z ) );
                }
            }
            FitTarget( center, extent );
        }
        else
        {
            // Material on a sphere. Geometry is built once and reused; clearing the runtime instances forces
            // a rebuild against the current material handle.
            smc.MeshHandle    = Assets::AssetHandle( static_cast<uint64_t>( 0 ) );
            smc.Primitive     = Geometry::PrimitiveType::Sphere;
            smc.MaterialSlots = { m_PendingHandle };
            smc.RuntimeMaterialInstances.clear();
            FitTarget( glm::vec3( 0.0f ), 1.0f );
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
                // GL-on-Vulkan renders Y-flipped (negative-height viewport); flip on write for an upright PNG.
                stbi_flip_vertically_on_write( 1 );
                stbi_write_png( m_PendingPng.c_str(), kSize, kSize, 4, out.data(), kSize * 4 );
            }
        }

        m_Phase = 0;
    }
} // namespace Desert::Editor
