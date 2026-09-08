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
        // Waits for the GPU before releasing the scene, then the renderer that owns its passes. The same
        // order and the same reason as ~PreviewViewport: this is destroyed when the thumbnail queue has been
        // idle for a while (so the renderer slot goes back), which can happen while the last frame this
        // recorded into is still executing against its pipelines and descriptor pools.
        ~AssetThumbnailRenderer();

        // Queue a material to be captured to outPng. Refuses (with the reason) when the handle is null or a
        // capture is already in flight — see RequestMesh for why these answer instead of returning void.
        // `flatPreview` previews on a camera-facing PLANE/card instead of a sphere — right for foliage/cutout
        // materials (a grass card atlas wraps/garbles on a sphere). Drives forward via Tick().
        [[nodiscard]] Common::BoolResultStr RequestMaterial( const Assets::AssetHandle& materialHandle,
                                                             const std::string& outPng, bool flatPreview = false );

        /**
         * @brief Queue a mesh, auto-framed by its bounds, to outPng. If `material` is non-null it is applied
         *        to every slot; otherwise the mesh's own submesh materials are used.
         *
         * IT ANSWERS, AND THAT IS THE POINT. The mesh has to be BUILT in the MeshService — the handle alone
         * is not enough — and until now a handle the service did not have was accepted in silence: Tick()
         * cleared the material slots, framed a unit box around nothing, and captured the empty backdrop.
         * A 200 KB PNG of blank sky was then written, its modification time moved, and every layer above
         * read that as success: ThumbnailService counted "1 captured", the freshness rule called the file a
         * current picture of the asset, and the row drew a square of sky forever. Measured on this tree,
         * with a real 44 MB mesh a scene had referenced but nothing had loaded.
         *
         * That is the contract's §1.4 exactly — an empty successful answer is a silent wrong answer — and
         * it cannot be fixed after the render, because a picture of an empty scene is a legitimate picture
         * of some assets. It has to be refused BEFORE the capture, where the reason is still known.
         */
        [[nodiscard]] Common::BoolResultStr
        RequestMesh( const Assets::AssetHandle& meshHandle, const std::string& outPng,
                     const Assets::AssetHandle& material = Assets::AssetHandle( static_cast<uint64_t>( 0 ) ) );

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
        // No camera entity: the capture goes through the scene's own EditorCamera, which Scene::Init
        // publishes as the main camera. See EnsureInit for why a CameraComponent here read as load-bearing
        // and was not.
        ECS::Entity                             m_Target;
        bool                                    m_Inited = false;

        Assets::AssetHandle m_PendingHandle{ static_cast<uint64_t>( 0 ) };
        Assets::AssetHandle m_PendingMaterial{ static_cast<uint64_t>( 0 ) }; // mesh's linked material (0 = default)
        std::string         m_PendingPng;
        bool                m_PendingIsMesh    = false; // false = material preview, true = mesh
        bool                m_PendingFlatPreview = false; // material on a camera-facing plane (foliage/cutout)
        int                 m_Phase = 0; // 0 = idle, else = remaining render frames (capture on the last)

        // THE PNG IS THE DISPLAY SIZE, and this used to be four times larger than anything could show.
        //
        // The old rule was "hi-res on disk, decoupled from the tiny on-screen size" — 1024 px written,
        // 2048 px rendered. But ThumbnailCache::Get is the ONLY reader of these files and it box-averages
        // every one of them down to kThumbMaxDim before it uploads anything, so the extra pixels were not
        // stored for later: they were decoded and thrown away on every load, in every session, forever.
        // Measured on this tree (Debug, and the machine was shared):
        //
        //   capture, final frame     2823 ms = 892 device idle + 1289 readback (16 MB) + 114 downscale
        //                            + 528 png encode
        //   cache HIT, per thumbnail   38 ms = 31 png decode (1024x1024) + 7 box filter and upload
        //   on disk                   961 KB per material, 106 materials in this project alone
        //
        // Two thirds of a capture and all of a cache hit were paid for resolution that never reached a
        // pixel. Matching kSize to kThumbMaxDim removes the load-time box filter entirely (the decode
        // lands at the size it is uploaded at) and quarters both the readback and the encode.
        //
        // NOT smaller than the display, which is the failure in the other direction: v3 exists because
        // 128 px "looked like 240p" in the grid. kThumbMaxDim was raised 256 -> 512 in this same change
        // (the largest grid card is 528 physical pixels on a 2x display — see ThumbnailCache.hpp for the
        // arithmetic), so what reaches the screen gets SHARPER here, not softer. This is the first version
        // in which the pixels stored are the pixels drawn.
        static constexpr uint32_t kSize         = 512;       // output PNG size == ThumbnailCache::kThumbMaxDim
        static constexpr uint32_t kRenderSize   = kSize * 2; // offscreen render size (2x supersample -> kSize)
        static constexpr int      kRenderFrames = 5;         // warm-up render frames before the capture readback
    };
} // namespace Desert::Editor
