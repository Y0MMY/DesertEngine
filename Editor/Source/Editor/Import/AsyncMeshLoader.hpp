#pragma once

#include "ImportManager.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace Desert::Editor
{
    // Header-only async cooker. Dropping a heavy mesh used to HITCH the editor because the Assimp cook
    // (parse FBX -> .stmesh/.tex) ran on the main thread. The cook is pure CPU + file I/O (NO GPU / no
    // AssetManager / no ECS), so it is safe to run on a worker thread. This class owns ONE worker that pops
    // cook jobs and runs them on its OWN ImportManager; the MAIN thread later drains completed cooks
    // (PollCompleted) and does all the GPU/AssetManager/ECS work (register + spawn). Clean thread split:
    //   - worker: m_Importer.Import(path)  (cook -> writes Cooked/*.stmesh + Cooked/Textures/*.tex)
    //   - main:   PollCompleted() -> ResolveOrImport (now cooked -> fast) -> assign to the entity
    // Progress (Total/Done) drives a UI bar; counters reset to 0 when everything has drained (idle).
    class AsyncMeshLoader
    {
    public:
        struct Done
        {
            std::string SourcePath; // the dropped mesh source
            uint64_t    UserData;   // caller token (e.g. the pending entity's UUID)
        };

        AsyncMeshLoader() : m_Worker( [this] { WorkerLoop(); } ) {} // m_Worker declared LAST -> starts last
        ~AsyncMeshLoader()
        {
            {
                std::lock_guard<std::mutex> lk( m_Mutex );
                m_Stop = true;
            }
            m_CV.notify_all();
            if ( m_Worker.joinable() )
                m_Worker.join();
        }

        AsyncMeshLoader( const AsyncMeshLoader& )            = delete;
        AsyncMeshLoader& operator=( const AsyncMeshLoader& ) = delete;

        // Queue a cook (main thread). Returns immediately; the worker does the heavy parse in the background.
        void Request( const std::string& sourcePath, uint64_t userData )
        {
            {
                std::lock_guard<std::mutex> lk( m_Mutex );
                m_Queue.push( { sourcePath, userData } );
            }
            ++m_Total;
            m_CV.notify_one();
        }

        // Main thread, once per frame: take everything the worker has finished cooking.
        std::vector<Done> PollCompleted()
        {
            std::vector<Done> out;
            {
                std::lock_guard<std::mutex> lk( m_Mutex );
                while ( !m_Completed.empty() )
                {
                    out.push_back( m_Completed.front() );
                    m_Completed.pop();
                }
                // Idle (nothing queued, nothing cooking, nothing left to hand out) -> reset progress.
                if ( m_Queue.empty() && !m_Active )
                {
                    m_Total = 0;
                    m_Done  = 0;
                }
            }
            return out;
        }

        bool IsBusy() const
        {
            std::lock_guard<std::mutex> lk( m_Mutex );
            return !m_Queue.empty() || m_Active || !m_Completed.empty();
        }
        int   Total() const { return m_Total.load(); }
        int   Done2() const { return m_Done.load(); }
        float Progress() const
        {
            const int t = m_Total.load();
            return t > 0 ? static_cast<float>( m_Done.load() ) / static_cast<float>( t ) : 0.0f;
        }

    private:
        void WorkerLoop()
        {
            for ( ;; )
            {
                Done job;
                {
                    std::unique_lock<std::mutex> lk( m_Mutex );
                    m_CV.wait( lk, [this] { return m_Stop || !m_Queue.empty(); } );
                    if ( m_Stop )
                        return;
                    job      = m_Queue.front();
                    m_Queue.pop();
                    m_Active = true;
                }

                // The heavy part, OFF the main thread: parse the source + write the cooked files. No GPU,
                // no AssetManager, no ECS — only this worker's own ImportManager + the filesystem.
                m_Importer.Import( job.SourcePath );

                {
                    std::lock_guard<std::mutex> lk( m_Mutex );
                    m_Completed.push( job );
                    m_Active = false;
                }
                ++m_Done;
            }
        }

        mutable std::mutex      m_Mutex;
        std::condition_variable m_CV;
        std::queue<Done>        m_Queue;     // pending cooks
        std::queue<Done>        m_Completed; // cooked, awaiting main-thread spawn
        bool                    m_Active = false;
        bool                    m_Stop   = false;
        std::atomic<int>        m_Total{ 0 };
        std::atomic<int>        m_Done{ 0 };
        ImportManager           m_Importer; // worker-only cooker (its own Assimp/texture importers)
        std::thread             m_Worker;   // MUST be the last member (constructed after everything it uses)
    };
} // namespace Desert::Editor
