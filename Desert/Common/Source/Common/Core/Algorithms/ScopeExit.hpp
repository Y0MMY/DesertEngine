#pragma once

#include <utility>

// Scope guard: runs a block of code when the enclosing scope unwinds
// (normal exit or exception). Usage:
//
//     SCOPE_EXIT
//     {
//         vkDestroyBuffer( device, staging, nullptr );
//     };
//
namespace Common::Algorithms
{
    template <typename F>
    class ScopeExit
    {
    public:
        explicit ScopeExit( F&& fn ) : m_Fn( std::move( fn ) )
        {
        }

        ~ScopeExit()
        {
            m_Fn();
        }

        ScopeExit( const ScopeExit& )            = delete;
        ScopeExit& operator=( const ScopeExit& ) = delete;
        ScopeExit( ScopeExit&& )                 = delete;
        ScopeExit& operator=( ScopeExit&& )      = delete;

    private:
        F m_Fn;
    };

    namespace Detail
    {
        struct ScopeExitTag
        {
        };

        template <typename F>
        ScopeExit<F> operator+( ScopeExitTag, F&& fn )
        {
            return ScopeExit<F>( std::forward<F>( fn ) );
        }
    } // namespace Detail
} // namespace Common::Algorithms

#define DESERT_SCOPE_EXIT_CONCAT_( a, b ) a##b
#define DESERT_SCOPE_EXIT_CONCAT( a, b ) DESERT_SCOPE_EXIT_CONCAT_( a, b )

#define SCOPE_EXIT                                                                                                \
    auto DESERT_SCOPE_EXIT_CONCAT( _scopeExit_, __LINE__ ) = ::Common::Algorithms::Detail::ScopeExitTag{} + [&]()
