#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace Common
{
    // Small-buffer callable for the job queue: closures up to kInlineSize bytes live INLINE in the
    // queue node — zero heap per job (std::function's SBO is too small for a typical ParallelFor
    // chunk, so every submit paid a heap alloc + free). Larger captures fall back to one heap
    // block; that stays correct, just the exception rather than the rule.
    class InlineJob
    {
    public:
        static constexpr std::size_t kInlineSize = 64;

        InlineJob() = default;

        template <typename F, typename D = std::decay_t<F>,
                  typename = std::enable_if_t<!std::is_same_v<D, InlineJob>>>
        InlineJob( F&& fn )
        {
            if constexpr ( sizeof( D ) <= kInlineSize && alignof( D ) <= alignof( std::max_align_t ) &&
                           std::is_nothrow_move_constructible_v<D> )
            {
                new ( m_Storage ) D( std::forward<F>( fn ) );
                m_Ops = &s_InlineOps<D>;
            }
            else
            {
                *reinterpret_cast<D**>( static_cast<void*>( m_Storage ) ) = new D( std::forward<F>( fn ) );
                m_Ops = &s_HeapOps<D>;
            }
        }

        InlineJob( InlineJob&& other ) noexcept
        {
            MoveFrom( other );
        }
        InlineJob& operator=( InlineJob&& other ) noexcept
        {
            if ( this != &other )
            {
                Destroy();
                MoveFrom( other );
            }
            return *this;
        }
        InlineJob( const InlineJob& )            = delete;
        InlineJob& operator=( const InlineJob& ) = delete;
        ~InlineJob()
        {
            Destroy();
        }

        void operator()()
        {
            m_Ops->Invoke( m_Storage );
        }
        explicit operator bool() const
        {
            return m_Ops != nullptr;
        }

    private:
        struct Ops
        {
            void ( *Invoke )( void* );
            void ( *MoveTo )( void* src, void* dst ); // dst is uninitialized; src is destroyed
            void ( *Destroy )( void* );
        };

        // std::destroy_at rather than an explicit `->~D()`: D is a lambda closure type here, and MSVC
        // rejects the destructor-call syntax on a template parameter naming a closure ("class has no
        // destructor called '~D'"). destroy_at is equivalent, standard, and accepted by both compilers.
        template <typename D>
        static constexpr Ops s_InlineOps = {
            []( void* s ) { ( *static_cast<D*>( s ) )(); },
            []( void* src, void* dst )
            {
                new ( dst ) D( std::move( *static_cast<D*>( src ) ) );
                std::destroy_at( static_cast<D*>( src ) );
            },
            []( void* s ) { std::destroy_at( static_cast<D*>( s ) ); } };

        template <typename D>
        static constexpr Ops s_HeapOps = {
            []( void* s ) { ( **static_cast<D**>( s ) )(); },
            []( void* src, void* dst )
            { *static_cast<D**>( dst ) = *static_cast<D**>( src ); },
            []( void* s ) { delete *static_cast<D**>( s ); } };

        void MoveFrom( InlineJob& other ) noexcept
        {
            m_Ops = other.m_Ops;
            if ( m_Ops )
                m_Ops->MoveTo( other.m_Storage, m_Storage );
            other.m_Ops = nullptr;
        }
        void Destroy()
        {
            if ( m_Ops )
            {
                m_Ops->Destroy( m_Storage );
                m_Ops = nullptr;
            }
        }

        alignas( std::max_align_t ) unsigned char m_Storage[kInlineSize];
        const Ops* m_Ops = nullptr;
    };

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

        // Fire-and-forget. Closures <= InlineJob::kInlineSize bytes enqueue with ZERO heap allocations.
        void Submit( InlineJob job );

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

        mutable std::mutex      m_Mutex;
        std::condition_variable m_CV;
        std::deque<InlineJob>   m_Queue;
        std::vector<std::thread>          m_Workers;
        size_t                            m_Running = 0; // jobs currently executing
        bool                              m_Stop    = false;
    };
} // namespace Common
