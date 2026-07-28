#pragma once

#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Assets/Common.hpp>

#include <memory>
#include <string>

namespace Desert::Editor
{
    // Renders small offscreen previews of assets (a material on a sphere) and writes them to PNG files on
    // disk, shown in the asset browser grid via the normal ThumbnailCache (persist across restarts).
    //
    // The offscreen render is recorded into the editor's in-flight frame command buffer, so a CPU readback
    // of "this frame's" render races the GPU. The reliable pattern (verified): render the SAME material for
    // two consecutive frames and read back on the second frame — the readback (after WaitDeviceIdle) returns
    // the FIRST frame's already-submitted render. One capture is in flight at a time.
    class AssetThumbnailRenderer
    {
    public:
        // Queue a material to be captured to outPng. No-op if a capture is already in flight (see HasPending).
        // `flatPreview` previews on a camera-facing PLANE/card instead of a sphere — right for foliage/cutout
        // materials (a grass card atlas wraps/garbles on a sphere). Drives forward via Tick().
        void RequestMaterial( const Assets::AssetHandle& materialHandle, const std::string& outPng,
                              bool flatPreview = false );

        // Queue a mesh (must be registered in the MeshService), auto-framed by its bounds, to outPng. If
        // `material` is non-null it's applied to every slot (the mesh's linked/sidecar material) so the
        // preview shows the real look; otherwise the default material is used.
        void RequestMesh( const Assets::AssetHandle& meshHandle, const std::string& outPng,
                          const Assets::AssetHandle& material = Assets::AssetHandle( static_cast<uint64_t>( 0 ) ) );

        // Queue a sphere rendered with a REGISTERED shader by name (the generic/data-driven
        // material path: MaterialComponent.ShaderName). The shader-graph editor's live preview.
        void RequestShader( const std::string& shaderName, const std::string& outPng );

        // Is a capture in flight? Gates requests to one at a time.
        [[nodiscard]] bool HasPending() const { return m_Phase != 0; }

        // Advance the capture state machine. Call ONCE per frame. Renders the pending material; on the
        // second frame it reads back the first frame's render and writes the PNG.
        void Tick();

    private:
        void EnsureInit();
        void FitTarget( const glm::vec3& center, float worldSize );
        void RecordRender();

        std::unique_ptr<Graphic::SceneRenderer> m_Renderer;
        // Fully qualified: a Desert::Editor::Core namespace also exists (ViewportMode/FoliagePaint), so an
        // unqualified Core::Scene would wrongly resolve there in TUs that see it.
        std::shared_ptr<::Desert::Core::Scene>  m_Scene;
        ECS::Entity                             m_Camera;
        ECS::Entity                             m_Target;
        bool                                    m_Inited = false;

        Assets::AssetHandle m_PendingHandle{ static_cast<uint64_t>( 0 ) };
        Assets::AssetHandle m_PendingMaterial{ static_cast<uint64_t>( 0 ) }; // mesh's linked material (0 = default)
        std::string         m_PendingPng;
        std::string         m_PendingShaderName; // non-empty = shader-by-name preview (generic path)
        bool                m_PendingIsMesh    = false; // false = material preview, true = mesh
        bool                m_PendingFlatPreview = false; // material on a camera-facing plane (foliage/cutout)
        int                 m_Phase = 0; // 0 = idle, else = remaining render frames (capture on the last)

        // High-res PNG on disk (crisp / reusable) — the grid loads it into a SMALL GPU texture for display
        // (ThumbnailCache::kThumbMaxDim), so storage quality is decoupled from the tiny on-screen size.
        static constexpr uint32_t kSize         = 1024;      // output PNG size (~1 MP square; was 256)
        static constexpr uint32_t kRenderSize   = kSize * 2; // offscreen render size (2x supersample -> kSize)
        static constexpr int      kRenderFrames = 5;         // warm-up render frames before the capture readback
    };
} // namespace Desert::Editor
