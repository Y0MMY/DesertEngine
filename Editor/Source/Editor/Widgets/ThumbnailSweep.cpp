#include "ThumbnailSweep.hpp"

#include <Editor/Widgets/ThumbnailService.hpp>
#include <Editor/Widgets/ThumbnailSubject.hpp>

#include <Common/Core/JobSystem.hpp>
#include <Common/Core/Logger.hpp>

#include <chrono>

namespace Desert::Editor
{
    // The half that needs the editor's device-bound service. Its sibling, ThumbnailScan.cpp, holds the
    // scan and the per-frame budget and includes none of this — see the note at the top of that file.

    void ThumbnailSweeper::Tick( Assets::AssetManager* manager, const std::filesystem::path& root )
    {
        if ( root.empty() )
            return;

        // A DIFFERENT PROJECT IS A DIFFERENT QUESTION. Without this a pass started under the old root
        // would go on queueing captures for files that are no longer in front of anybody, and — because
        // the paths are absolute — would keep the old project's thumbnail cache warm instead of the new
        // one's.
        if ( m_Root != root )
        {
            Reset();
            m_Root = root;
        }

        // ── Collect ───────────────────────────────────────────────────────────────────────────────────
        if ( m_Scan.valid() && m_Scan.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
        {
            m_Pending        = m_Scan.get();
            m_Scan           = {};
            m_Next           = 0;
            m_QueuedThisPass = 0;
            m_Announced      = false;
        }

        // ── Hand out this frame's share ───────────────────────────────────────────────────────────────
        if ( !m_Pending.empty() )
        {
            m_QueuedThisPass += Drain(
                 [manager]( const ThumbnailSweepCandidate& candidate )
                 {
                     switch ( candidate.By )
                     {
                         case ThumbnailFormats::Producer::Painted:
                             // No manager, no handle, no renderer: the painter opens the file itself.
                             ThumbnailService::Get().RequestPainted( candidate.AssetPath );
                             return;

                         case ThumbnailFormats::Producer::RenderedMaterial:
                         {
                             if ( !manager )
                                 return;
                             const auto subject =
                                  ThumbnailSubject::ResolveMaterial( *manager, candidate.AssetPath );
                             if ( !subject )
                             {
                                 // A background pass must not shout. This is the ONLY line a failed
                                 // background resolve produces, it is a warning rather than an error
                                 // because nobody asked for this asset by name, and it happens once —
                                 // the service's failure set remembers an asset it could not queue.
                                 LOG_WARN( "[Thumbnails] sweep skipped '{}': {}", candidate.AssetPath,
                                           subject.GetError() );
                                 return;
                             }
                             ThumbnailService::Get().RequestMaterial(
                                  subject.GetValue().Handle, candidate.AssetPath, subject.GetValue().Flat );
                             return;
                         }

                         case ThumbnailFormats::Producer::RenderedMesh:
                         {
                             if ( !manager )
                                 return;
                             const auto subject =
                                  ThumbnailSubject::ResolveMesh( *manager, candidate.AssetPath );
                             if ( !subject )
                             {
                                 // AN UNCOOKED MESH IS THE COMMON CASE AND IS NOT NEWS. Logging it would
                                 // put one line per uncooked source in the log every three seconds for
                                 // the life of the session, which is how a log stops being read.
                                 return;
                             }
                             ThumbnailService::Get().RequestMesh( subject.GetValue().Handle,
                                                                  subject.GetValue().CookedPath,
                                                                  subject.GetValue().Material );
                             return;
                         }

                         case ThumbnailFormats::Producer::Decoded:
                         case ThumbnailFormats::Producer::Authored:
                         case ThumbnailFormats::Producer::None:
                             // Unreachable: ScanForMissingThumbnails only emits generated producers. No
                             // default label, so a fifth producer added to the census makes THIS switch
                             // fail to compile rather than silently drop every asset of the new kind.
                             return;
                     }
                 } );

            if ( m_Pending.empty() && !m_Announced )
            {
                m_Announced = true;
                if ( m_QueuedThisPass > 0 )
                {
                    LOG_INFO( "[Thumbnails] background sweep of '{}' queued {} asset(s) with no usable "
                              "picture. Nothing was clicked; they render as slots and workers come free.",
                              m_Root.generic_string(), m_QueuedThisPass );
                }
            }
            return; // one thing per frame: hand out, or scan. Never both.
        }

        // ── Start the next pass ───────────────────────────────────────────────────────────────────────
        if ( m_Scan.valid() )
            return;

        if ( --m_FramesUntilScan > 0 )
            return;
        m_FramesUntilScan = kFramesBetweenScans;

        // THROUGH THE JobSystem. A directory walk plus two stats per file is not free, and the one thing
        // it must never do is happen on the frame's thread — a 30 ms hitch every three seconds is exactly
        // the sort of cost a background convenience has no right to charge. `Async` captures the root by
        // value, so the future owns everything it reads.
        m_Scan = Common::JobSystem::Get().Async(
             [root = m_Root] { return ScanForMissingThumbnails( root, kCandidatesPerScan ); } );
    }
} // namespace Desert::Editor
