#include "ThumbnailSweep.hpp"

#include <Editor/Import/CookPaths.hpp>
#include <Editor/Widgets/ThumbnailCache.hpp>
#include <Editor/Widgets/ThumbnailFreshness.hpp>
#include <Editor/Widgets/ThumbnailService.hpp>
#include <Editor/Widgets/ThumbnailSubject.hpp>

#include <Common/Core/JobSystem.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <chrono>

namespace Desert::Editor
{
    std::vector<ThumbnailSweepCandidate> ScanForMissingThumbnails( const std::filesystem::path& root,
                                                                   std::size_t                  limit )
    {
        std::vector<ThumbnailSweepCandidate> out;
        if ( root.empty() || limit == 0 )
            return out;

        // THE ONE ENUMERATION. `ListFilesRecursive` is what every content scanner in this engine uses,
        // including the preloader, and it is the only one that also sees a mounted `.dpak` — the font and
        // icon services each hand-rolled the disk half once and a packaged game scanned nothing.
        //
        // SAFE ON A WORKER **HERE**, and the qualification is the point. It consults the VFS mount stack,
        // which carries no lock; the stack is written by `VFS::MountPak`, whose only caller in this tree
        // is Runtime/Source/PackagedContent.cpp — the packaged game's startup. The editor never mounts a
        // pak, so in the process this sweep runs in the stack is empty and immutable for the session. If
        // an editor ever gains a mount, this call has to move or the VFS has to gain a lock.
        for ( const std::filesystem::path& file : Common::Utils::FileSystem::ListFilesRecursive( root ) )
        {
            if ( out.size() >= limit )
                break;

            const std::string path = file.generic_string();

            const ThumbnailFormats::Format* format =
                 ThumbnailFormats::Find( ThumbnailFormats::ExtensionOf( path ) );

            // Unknown to the browser, or a format whose picture is not generated: a decoded image is
            // already its own picture, an authored one is a person's decision, and a `None` row has a
            // written reason there must be no picture at all. All three are answers, and none of them is
            // work.
            if ( !format || !ThumbnailFormats::IsGenerated( format->By ) )
                continue;

            ThumbnailSweepCandidate candidate;
            candidate.AssetPath = path;
            candidate.By        = format->By;

            // The mesh is the one format whose picture is of a DIFFERENT file. A pure path computation
            // (fs::relative and a string replace, no stat), so it is free to do here; whether the cook
            // exists is asked at drain time, where the refusal can be reported with the manager's own
            // words rather than guessed at from a missing file.
            candidate.Subject = format->By == ThumbnailFormats::Producer::RenderedMesh
                                     ? CookPaths::CookedMesh( file, ".stmesh" ).generic_string()
                                     : path;

            candidate.Png = ThumbnailCache::DiskPath( candidate.Subject );

            // THE SAME QUESTION EVERY READER ASKS. Not `exists(png)`: that is the gate M8 found had
            // drifted away from the readers' rule, leaving assets that were neither drawn nor scheduled
            // for the life of the project.
            if ( ThumbnailFreshness::Judge(
                      ThumbnailFreshness::Observe( candidate.Png, candidate.Subject ) ) !=
                 ThumbnailFreshness::Verdict::Capture )
                continue;

            out.push_back( std::move( candidate ) );
        }
        return out;
    }

    void ThumbnailSweeper::Reset()
    {
        // The future is left to finish on its own: it captures a path by value and touches nothing this
        // object owns, so abandoning it costs one worker a directory walk. Waiting instead would block
        // the frame in which a person changed project, to collect an answer about the project they left.
        m_Scan = {};
        m_Pending.clear();
        m_Next            = 0;
        m_FramesUntilScan = 0;
        m_QueuedThisPass  = 0;
        m_Announced       = false;
    }

    void ThumbnailSweeper::SetPendingForTest( std::vector<ThumbnailSweepCandidate> pending )
    {
        m_Pending = std::move( pending );
        m_Next    = 0;
    }

    int ThumbnailSweeper::Drain( const std::function<void( const ThumbnailSweepCandidate& )>& sink )
    {
        int handed = 0;
        while ( m_Next < m_Pending.size() && handed < kRequestsPerFrame )
        {
            sink( m_Pending[m_Next] );
            ++m_Next;
            ++handed;
        }
        if ( m_Next >= m_Pending.size() )
        {
            m_Pending.clear();
            m_Next = 0;
        }
        return handed;
    }

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
