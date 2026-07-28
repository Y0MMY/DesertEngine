#include <Common/Core/JobSystem.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <numeric>
#include <vector>

// The pool is a process-global singleton — tests share it, which is exactly how the engine uses it.

TEST( JobSystem, SubmitRunsJobs )
{
    std::atomic<int> counter{ 0 };
    constexpr int    kJobs = 64;

    std::atomic<int> done{ 0 };
    for ( int i = 0; i < kJobs; ++i )
        Common::JobSystem::Get().Submit(
             [&]
             {
                 counter.fetch_add( 1 );
                 done.fetch_add( 1 );
             } );

    while ( done.load() < kJobs )
        std::this_thread::yield();

    EXPECT_EQ( counter.load(), kJobs );
}

TEST( JobSystem, AsyncReturnsValue )
{
    auto fut = Common::JobSystem::Get().Async( [] { return 7 * 6; } );
    EXPECT_EQ( fut.get(), 42 );
}

TEST( JobSystem, ParallelForCoversEveryIndexExactlyOnce )
{
    constexpr size_t      kCount = 10007; // prime: exercises the uneven-chunk remainder path
    std::vector<std::atomic<int>> hits( kCount );

    Common::JobSystem::Get().ParallelFor( kCount, [&]( size_t i ) { hits[i].fetch_add( 1 ); } );

    for ( size_t i = 0; i < kCount; ++i )
        ASSERT_EQ( hits[i].load(), 1 ) << "index " << i;
}

TEST( JobSystem, ParallelForZeroAndOne )
{
    std::atomic<int> calls{ 0 };
    Common::JobSystem::Get().ParallelFor( 0, [&]( size_t ) { calls.fetch_add( 1 ); } );
    EXPECT_EQ( calls.load(), 0 );

    Common::JobSystem::Get().ParallelFor( 1, [&]( size_t ) { calls.fetch_add( 1 ); } );
    EXPECT_EQ( calls.load(), 1 );
}

TEST( JobSystem, ParallelForComputesCorrectSum )
{
    constexpr size_t   kCount = 4096;
    std::vector<long>  values( kCount );

    Common::JobSystem::Get().ParallelFor( kCount,
                                          [&]( size_t i ) { values[i] = static_cast<long>( i ) * 2; } );

    const long sum      = std::accumulate( values.begin(), values.end(), 0L );
    const long expected = static_cast<long>( kCount ) * ( kCount - 1 ); // 2 * sum(0..n-1)
    EXPECT_EQ( sum, expected );
}

TEST( JobSystem, NestedSubmitFromWorker )
{
    // A job submitting another job must not deadlock (Pump-style chaining relies on this).
    std::atomic<bool> innerRan{ false };
    std::atomic<bool> outerDone{ false };

    Common::JobSystem::Get().Submit(
         [&]
         {
             Common::JobSystem::Get().Submit( [&] { innerRan.store( true ); } );
             outerDone.store( true );
         } );

    while ( !outerDone.load() || !innerRan.load() )
        std::this_thread::yield();

    SUCCEED();
}

TEST( JobSystem, WorkerCountIsPositive )
{
    EXPECT_GE( Common::JobSystem::Get().WorkerCount(), static_cast<size_t>( 1 ) );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    const int result = RUN_ALL_TESTS();
    Common::JobSystem::Get().Shutdown(); // join workers before static destruction
    return result;
}
