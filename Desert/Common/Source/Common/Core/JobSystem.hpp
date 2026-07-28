#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace Common
{
    // Engine-wide worker-thread pool — THE place for CPU-parallel work (asset cooking, LUT generation,
    // background mesh cooks, future parallel ECS). Replaces the ad-hoc one-off std::thread/std::async
    // sprinkled around the codebase so thread count stays bounded (workers = cores - 1) and work is
    // observable in one system.
    //
    // Threading contract:
    //   - Submit()/Async() are safe from any thread; jobs may run on any worker in any order.
    //   - ParallelFor() BLOCKS until done and the CALLING thread works too — safe to call from the main
    //     thread even when every worker is busy (it can never deadlock on a saturated pool).
    //   - Jobs must not assume GPU/AssetManager/ECS access is safe — same rule the old ad-hoc threads had.
    class JobSystem
    {
    public:
        // Global pool, started lazily on first use; joined automatically at exit (or via Shutdown()).
        static JobSystem& Get();

        ~JobSystem();
        JobSystem( const JobSystem& )            = delete;
        JobSystem& operator=( const JobSystem& ) = delete;

        // Fire-and-forget.
        void Submit( std::function<void()> job );

        // Submit with a result: returns a std::future for the callable's return value.
        template <typename F>
        auto Async( F&& fn ) -> std::future<std::invoke_result_t<F>>
        {
            using R   = std::invoke_result_t<F>;
            auto task = std::make_shared<std::packaged_task<R()>>( std::forward<F>( fn ) );
            auto fut  = task->get_future();
            Submit( [task] { ( *task )(); } );
            return fut;
        }

        // Blocking parallel-for over [0, count): body(index). Work is split into (workers + 1) contiguous
        // chunks; the calling thread executes one chunk itself while the pool takes the rest.
        void ParallelFor( size_t count, const std::function<void( size_t )>& body );

        size_t WorkerCount() const
        {
            return m_Workers.size();
        }

        // Pending + currently-running job count (approximate; for tests/diagnostics).
        size_t PendingJobs() const;

        // Stops accepting jobs, drains the queue and joins the workers. Idempotent.
        void Shutdown();

    private:
        JobSystem();
        void WorkerLoop();

        mutable std::mutex                m_Mutex;
        std::condition_variable           m_CV;
        std::deque<std::function<void()>> m_Queue;
        std::vector<std::thread>          m_Workers;
        size_t                            m_Running = 0; // jobs currently executing
        bool                              m_Stop    = false;
    };
} // namespace Common
