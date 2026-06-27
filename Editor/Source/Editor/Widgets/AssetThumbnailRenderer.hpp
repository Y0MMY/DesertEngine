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
        // Queue a material (on a sphere) to be captured to outPng. No-op if a capture is already in flight
        // (see HasPending). Drives forward via Tick().
        void RequestMaterial( const Assets::AssetHandle& materialHandle, const std::string& outPng );

        // Queue a mesh (must be registered in the MeshService), auto-framed by its bounds, to outPng.
        void RequestMesh( const Assets::AssetHandle& meshHandle, const std::string& outPng );

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
        std::shared_ptr<Core::Scene>            m_Scene;
        ECS::Entity                             m_Camera;
        ECS::Entity                             m_Target;
        bool                                    m_Inited = false;

        Assets::AssetHandle m_PendingHandle{ static_cast<uint64_t>( 0 ) };
        std::string         m_PendingPng;
        bool                m_PendingIsMesh = false; // false = material-on-sphere, true = mesh
        int                 m_Phase = 0; // 0 = idle, else = remaining render frames (capture on the last)

        static constexpr uint32_t kSize         = 128;       // output PNG size
        static constexpr uint32_t kRenderSize   = kSize * 2; // offscreen render size (2x supersample -> kSize)
        static constexpr int      kRenderFrames = 5;         // warm-up render frames before the capture readback
    };
} // namespace Desert::Editor
