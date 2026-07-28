#include "JobSystem.hpp"

#include <algorithm>
#include <atomic>

namespace Common
{
    JobSystem& JobSystem::Get()
    {
        static JobSystem s_Instance;
        return s_Instance;
    }

    JobSystem::JobSystem()
    {
        // cores - 1: leave the main thread its own core. At least one worker so Submit() always makes
        // progress on single-core machines.
        const unsigned hw      = std::thread::hardware_concurrency();
        const size_t   workers = std::max( 1u, hw > 1 ? hw - 1 : 1u );
        m_Workers.reserve( workers );
        for ( size_t i = 0; i < workers; ++i )
            m_Workers.emplace_back( [this] { WorkerLoop(); } );
    }

    JobSystem::~JobSystem()
    {
        Shutdown();
    }

    void JobSystem::WorkerLoop()
    {
        for ( ;; )
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk( m_Mutex );
                m_CV.wait( lk, [this] { return m_Stop || !m_Queue.empty(); } );
                if ( m_Stop && m_Queue.empty() )
                    return;
                job = std::move( m_Queue.front() );
                m_Queue.pop_front();
                ++m_Running;
            }

            job();

            {
                std::lock_guard<std::mutex> lk( m_Mutex );
                --m_Running;
            }
        }
    }

    void JobSystem::Submit( std::function<void()> job )
    {
        {
            std::lock_guard<std::mutex> lk( m_Mutex );
            if ( m_Stop )
                return; // shutting down: drop silently (jobs must not matter past shutdown)
            m_Queue.push_back( std::move( job ) );
        }
        m_CV.notify_one();
    }

    size_t JobSystem::PendingJobs() const
    {
        std::lock_guard<std::mutex> lk( m_Mutex );
        return m_Queue.size() + m_Running;
    }

    void JobSystem::ParallelFor( size_t count, const std::function<void( size_t )>& body )
    {
        if ( count == 0 )
            return;

        const size_t parts = std::min( count, m_Workers.size() + 1 );
        if ( parts <= 1 )
        {
            for ( size_t i = 0; i < count; ++i )
                body( i );
            return;
        }

        // Contiguous chunk per participant; the last chunk absorbs the remainder.
        const size_t                                 chunk = count / parts;
        std::vector<std::pair<size_t, size_t>>       ranges;
        for ( size_t p = 0; p < parts; ++p )
        {
            const size_t begin = p * chunk;
            const size_t end   = ( p + 1 == parts ) ? count : begin + chunk;
            if ( begin < end )
                ranges.emplace_back( begin, end );
        }

        // Chunks [1..N) go to the pool; the CALLING thread runs chunk 0 (so a saturated pool can never
        // deadlock this call — worst case everything runs right here, just serially).
        std::atomic<size_t>     remaining( ranges.size() - 1 );
        std::mutex              doneMutex;
        std::condition_variable doneCV;

        for ( size_t r = 1; r < ranges.size(); ++r )
        {
            const auto [begin, end] = ranges[r];
            Submit(
                 [&, begin, end]
                 {
                     for ( size_t i = begin; i < end; ++i )
                         body( i );
                     if ( remaining.fetch_sub( 1 ) == 1 )
                     {
                         std::lock_guard<std::mutex> lk( doneMutex );
                         doneCV.notify_one();
                     }
                 } );
        }

        for ( size_t i = ranges[0].first; i < ranges[0].second; ++i )
            body( i );

        std::unique_lock<std::mutex> lk( doneMutex );
        doneCV.wait( lk, [&] { return remaining.load() == 0; } );
    }

    void JobSystem::Shutdown()
    {
        {
            std::lock_guard<std::mutex> lk( m_Mutex );
            if ( m_Stop )
                return;
            m_Stop = true;
        }
        m_CV.notify_all();
        for ( auto& worker : m_Workers )
            if ( worker.joinable() )
                worker.join();
        m_Workers.clear();
    }
} // namespace Common
