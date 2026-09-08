#pragma once

#include <Editor/Widgets/AssetThumbnailRenderer.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Desert::Editor
{
    /**
     * @brief One thumbnail renderer for the whole editor.
     *
     * Every panel that wanted an asset preview used to build its OWN AssetThumbnailRenderer, and each of
     * those owns a full Graphic::SceneRenderer. Three panels meant three extra scene renderers, each
     * ticking its own single-slot queue, each unaware that the panel next door had already captured the
     * same material. A measured scene render costs ~2.4 ms, so this was not free.
     *
     * It also produced a visible wart: the Details material slot deliberately did no rendering at all
     * (to avoid becoming the fourth renderer) and fell back to a flat colour swatch for any material the
     * asset browser had never happened to show. A material could sit there as a coloured square forever.
     *
     * This service is the single owner. Panels REQUEST and read; EditorLayer ticks it once per frame.
     *
     * Requests are deduplicated across panels and across frames:
     *   - a PNG on disk that is still a picture OF its asset is never re-rendered (that is the persistent
     *     cache). "Still a picture of it" is Editor/Widgets/ThumbnailFreshness.hpp, the SAME rule every
     *     panel uses to decide whether to draw the file — not "the file exists", which is what this gate
     *     used to ask. The two questions differ for exactly the assets that need re-rendering, and while
     *     they differed those assets were neither drawn nor queued, permanently;
     *   - a request already queued or in flight is not queued twice, and one that has become unnecessary
     *     while it waited is dropped at dispatch rather than re-rendered;
     *   - an asset that failed to render is remembered and not retried, so a broken .demat cannot make
     *     the queue spin on it every frame forever. That memory is per-PROCESS on purpose: the usual
     *     reason a capture fails is a shader, a service registration or a device that was not ready yet,
     *     and persisting "this asset is bad" would turn a transient failure into one only a cache wipe
     *     could clear. The thing worth persisting is the picture, and that is what the PNG is.
     *
     * AND IT NEVER TAKES THE LAST RENDERER SLOT. A capture owns a full SceneRenderer, which is one of six
     * (Engine/Core/RendererSlotPool.hpp), and a renderer that finds none free does not fail — it records
     * into slot 0 and shares the main viewport's per-frame state. This queue is background work: nobody
     * clicked for it, and what it produces is the picture a row shows precisely WHILE the person cannot
     * have a live preview. Taking the sixth slot would therefore starve the surface they are opening in
     * order to render its consolation prize. The entitlement is stated once, for both consumers of it, in
     * Editor/Widgets/PreviewSlotBudget.hpp; when it says no, the queue is kept and the refusal is LOGGED,
     * because a queue that quietly stops draining reads exactly like a queue with nothing in it.
     */
    class ThumbnailService
    {
    public:
        static ThumbnailService& Get();

        // Queue a material preview if it is not already cached, queued or known-bad. Returns the PNG path
        // to read (which may not exist yet — draw a placeholder until it does).
        // `flatPreview` renders on a camera-facing card instead of a sphere (right for foliage/cutout).
        std::string RequestMaterial( const Assets::AssetHandle& material, const std::string& assetPath,
                                     bool flatPreview = false );

        // Queue a mesh preview, optionally with the material to apply to every slot.
        std::string RequestMesh( const Assets::AssetHandle& mesh, const std::string& assetPath,
                                 const Assets::AssetHandle& material = Assets::AssetHandle(
                                      static_cast<uint64_t>( 0 ) ) );

        // Drive the capture state machine. Called ONCE per frame by EditorLayer — not by panels, so a
        // hidden or closed panel neither starves nor double-ticks it.
        void Tick();

        // Forget a cached/failed result, e.g. after the asset was edited.
        void Invalidate( const std::string& assetPath );

        /**
         * @brief Release the renderer NOW, while the device is still alive. Called from
         *        EditorLayer::OnDetach.
         *
         * WHY THIS IS SAID OUT LOUD INSTEAD OF LEFT TO THE DESTRUCTOR. This service is a function-static:
         * it is destroyed at `__cxa_finalize`, after main has returned and after ~Application has taken the
         * device and the VMA allocator with it. ~AssetThumbnailRenderer's first act is
         * Renderer::WaitDeviceIdle(), which then dereferences a null s_RendererAPI and segfaults — measured,
         * exit 139, with the backtrace naming exactly this chain:
         *
         *     Renderer::WaitDeviceIdle <- ~AssetThumbnailRenderer <- ~ThumbnailService
         *     <- __cxa_finalize_ranges <- exit
         *
         * This is the same family 0bfdeccf fixed for the engine-side registries ("process-lifetime caches
         * became the next thing to outlive the device"), and this is the member that fix missed. It was
         * missed for a understandable reason: EditorLayer::OnDetach already names "asset thumbnails" among
         * the GPU objects it tears down, but that comment is about the PANELS, and this service is a peer of
         * the panels rather than one of them — no m_Panels.clear() can reach it.
         *
         * A static destructor cannot be ordered against the device, so ordering is not something to get
         * right here; it is something to stop relying on. After this call the destructor has nothing left to
         * do, which is the point — the release is deterministic, not merely early.
         *
         * NOT a guard inside ~AssetThumbnailRenderer that skips the wait when the device is gone: that would
         * turn a broken teardown order into a silent one, and a leak that never reports itself is worse than
         * the crash that does.
         *
         * Idempotent, and NOT a one-way switch: the service is usable again afterwards, because this is the
         * same release the 300-idle-frame path performs and that path must keep working mid-session.
         */
        void Shutdown();

        [[nodiscard]] bool HasWork() const
        {
            return !m_Queue.empty() || ( m_Renderer && m_Renderer->HasPending() );
        }

    private:
        enum class Kind
        {
            Material,
            Mesh
        };
        struct Request
        {
            Kind                Type = Kind::Material;
            Assets::AssetHandle Handle{ static_cast<uint64_t>( 0 ) };
            Assets::AssetHandle Material{ static_cast<uint64_t>( 0 ) }; // meshes only
            std::string         Identity; // ThumbnailKey::Identity of the asset, NOT a path spelling
            // The asset's own file. Carried so the freshness question can be asked AGAIN at dispatch: a
            // request can sit in this queue for seconds, and in that time the capture it asks for may
            // already have been done (two panels showing one asset) or made unnecessary.
            std::string Source;
            std::string Png;
            bool        Flat = false;
        };

        // Shared by both Request* entry points: decides whether the work is needed at all. Takes the
        // asset's IDENTITY (ThumbnailKey::Identity), never a raw path — the sets below are keyed on it.
        bool ShouldQueue( const std::string& identity, const std::string& png, const std::string& source );

        // The identity-free half of the question: is the PICTURE on disk missing or out of date? Split out
        // because dispatch asks it a second time, when the dedup sets deliberately still hold the entry.
        static bool NeedsCapture( const std::string& png, const std::string& source );

        /**
         * @brief Build the renderer — but only if a background job is entitled to a slot right now.
         *
         * The whole reason this is a function and not two lines in Tick(): the rule that a capture must
         * never take the LAST free renderer slot is a standing condition, and a condition written at the
         * one call site it happens to have today is a condition the second call site will not have. False
         * means "not now"; the queue is left standing and the refusal is logged, because a queue that
         * silently stops draining looks exactly like a queue with nothing in it.
         */
        bool AcquireRenderer();

        // Created lazily — a session may never preview — and RELEASED again once the queue has been idle
        // for a while, because it owns a full SceneRenderer and therefore one of the six renderer slots
        // (Engine/Core/RendererSlotPool.hpp). Holding it for the rest of the session after one thumbnail
        // meant an editor that had ever shown the asset browser had five slots, not six, for the surfaces
        // the user actually opens; past six, a scene view records into slot 0 and shares the main
        // viewport's camera with no error message at all.
        std::unique_ptr<AssetThumbnailRenderer> m_Renderer;
        std::vector<Request>                    m_Queue;
        // Keyed on ThumbnailKey::Identity, not on a path spelling, so two panels naming one asset
        // differently cannot each hold their own entry (see Invalidate).
        std::unordered_set<std::string>         m_Queued;  // asset identities currently queued or in flight
        std::unordered_set<std::string>         m_Failed;  // gave up: do not retry every frame
        std::string                             m_InFlight;      // identity of the asset being captured
        std::string                             m_InFlightPng;   // its target PNG, checked on completion
        // The target's modification time BEFORE the capture started, absent when there was no file. The
        // completion test compares against it rather than asking whether the file exists — see Tick().
        std::optional<std::filesystem::file_time_type> m_InFlightPngBefore;
        int                                            m_InFlightTicks = 0;
        int                                            m_IdleTicks     = 0; // consecutive frames with no work
        // Already said out loud that there was no slot to spare. Latched so the warning is one line per
        // stretch of scarcity rather than one per frame, and cleared — with its own line — the moment one
        // comes free, because "it is running again" is as much news as "it stopped".
        bool m_SlotRefused = false;

        // What this run of the queue did, reported once when it drains. A capture that succeeds used to
        // say nothing at all, so "the editor is rendering previews" and "the editor has stopped bothering"
        // looked identical in a log — and the second is what M8 was reported as.
        int m_Captured = 0;
        int m_Skipped  = 0; // queued, then found already fresh before it was dispatched

        static std::optional<std::filesystem::file_time_type> PngStamp( const std::string& png );
    };
} // namespace Desert::Editor
