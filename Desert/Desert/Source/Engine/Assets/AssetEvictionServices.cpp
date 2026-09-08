#include <Engine/Assets/AssetEviction.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Logger.hpp>

namespace Desert::Assets
{
    namespace
    {
        /// THE ONLY PLACE IN THE EVICTION PATH THAT REACHES THE GPU LAYER, and it is four forwarding calls.
        ///
        /// Everything else — which assets are unreachable, which refuse, what the outcome says — lives in
        /// AssetEviction.cpp and compiles without Vulkan, which is what lets the suite drive the whole
        /// sweep against a recording double and assert the exact set of handles it released.
        class ServiceEvictionSink final : public IEvictionSink
        {
        public:
            bool DropBuiltMesh( const Common::AssetHandle& handle ) override
            {
                return Runtime::ResourceRegistry::GetMeshService()->EvictBuilt( handle );
            }

            bool HasBuiltMaterial( const Common::AssetHandle& handle ) const override
            {
                return Runtime::ResourceRegistry::GetMaterialService()->HasBuiltMaterial( handle );
            }

            void DropBuiltMaterial( const Common::AssetHandle& handle ) override
            {
                Runtime::ResourceRegistry::GetMaterialService()->Invalidate( handle );
            }

            void CollectGarbage() override
            {
                Runtime::ResourceRegistry::GetMaterialService()->CollectGarbage();
            }
        };
    } // namespace

    IEvictionSink& EngineEvictionSink()
    {
        static ServiceEvictionSink sink;
        return sink;
    }

    // ────────────────────────────────────────────────────────────────────────────────────────────────
    // The trigger
    // ────────────────────────────────────────────────────────────────────────────────────────────────

    namespace
    {
        /// FRAMES OF QUIET BEFORE THE SWEEP RUNS, and this number is the whole of the debounce.
        ///
        /// It was NOT there in the first working version, and the log said why it had to be. Loading a
        /// level is not one event: the editor builds an empty scene and initialises it, then deserialises
        /// the file into it and initialises it again, and on a big level those land in DIFFERENT frames.
        /// A sweep fired on the first of them sees a world with almost nothing in it, releases what the
        /// half-loaded level is about to ask for, and the load pays to read it all back. Measured on a
        /// sixteen-scene session: two sweeps 106 ms apart for one scene change, the first seeing 17 roots
        /// and the second 5 — two different answers to one question, and the smaller one won.
        ///
        /// Every request RE-ARMS the countdown, so a load that spans five frames sweeps once, after it.
        /// Two frames rather than one because the second Init of a load lands in the frame after the
        /// first, and one would still fire between them.
        constexpr int kQuietFramesBeforeSweep = 2;

        // A countdown and a reason. Function-local statics so the schedule is usable from a static
        // initialiser, and so a headless suite can drive it without an Application.
        int& FramesUntilSweep()
        {
            static int frames = 0; // 0 = nothing pending
            return frames;
        }

        std::string& SweepReason()
        {
            static std::string reason;
            return reason;
        }
    } // namespace

    void AssetEvictionSchedule::Request( std::string why )
    {
        // The FIRST reason wins, for the same reason AssetRootSet keeps the first: several scene loads can
        // land between two frames (opening a level closes the previous one), and the one that started the
        // sequence is the one a reader is looking for.
        if ( FramesUntilSweep() == 0 )
            SweepReason() = std::move( why );
        FramesUntilSweep() = kQuietFramesBeforeSweep;
    }

    bool AssetEvictionSchedule::IsDue()
    {
        return FramesUntilSweep() > 0;
    }

    void AssetEvictionSchedule::RunIfDue( const std::function<AssetRootSet()>& collectRoots )
    {
        if ( FramesUntilSweep() == 0 )
            return;

        // The quiet frames the debounce above is about. A request during one of them starts the count
        // again, so this only reaches zero when nothing has asked for a sweep for two whole frames.
        if ( --FramesUntilSweep() > 0 )
            return;

        // Cleared BEFORE the sweep, not after. A sweep that threw or that a future author made re-entrant
        // would otherwise run for ever, and "the editor froze after changing scenes" is a much worse
        // failure than one missed sweep.
        FramesUntilSweep()    = 0;
        const std::string why = std::move( SweepReason() );
        SweepReason()         = std::string();

        if ( !collectRoots )
            return;

        const AssetRootSet roots = collectRoots();

        for ( AssetManager* manager : AssetManager::LiveManagers() )
        {
            if ( manager == nullptr )
                continue;

            const EvictionOutcome outcome = AssetEviction::Run( *manager, roots, EngineEvictionSink() );

            // ONE LINE PER SWEEP, ALWAYS, INCLUDING THE SWEEPS THAT RELEASED NOTHING. A sweep that frees
            // nothing because everything is still reachable and a sweep that frees nothing because its
            // root walk is broken produce the same memory graph, and only this line separates them: the
            // first reports a large `Reachable`, the second reports `Reachable` equal to a handful of
            // roots. That distinction is the whole reason the outcome carries counts rather than a bool.
            LOG_INFO( "[Assets] eviction ({}): {}", why, outcome.Describe() );
        }
    }

} // namespace Desert::Assets
