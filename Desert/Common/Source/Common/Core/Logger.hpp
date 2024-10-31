#pragma once

#include <spdlog/spdlog.h>

namespace Common::Logger
{
    inline void LogInit()
    {
        spdlog::set_pattern( "%^[%T][Desert]: %v%$" );
    }

    template <typename... Args>
    constexpr void LogDebug( spdlog::format_string_t<Args...> InFormat, Args&&... InArgs )
    {
        spdlog::set_level( spdlog::level::debug );
        spdlog::debug( InFormat, std::forward<Args>( InArgs )... );
    }

    template <typename... Args>
    constexpr void LogInfo( spdlog::format_string_t<Args...> InFormat, Args&&... InArgs )
    {
        spdlog::set_level( spdlog::level::info );
        spdlog::info( InFormat, std::forward<Args>( InArgs )... );
    }

    template <typename... Args>
    constexpr void LogWarn( spdlog::format_string_t<Args...> InFormat, Args&&... InArgs )
    {
        spdlog::set_level( spdlog::level::warn );
        spdlog::warn( InFormat, std::forward<Args>( InArgs )... );
    }

    template <typename... Args>
    constexpr void LogError( spdlog::format_string_t<Args...> InFormat, Args&&... InArgs )
    {
        spdlog::set_level( spdlog::level::err );
        spdlog::error( InFormat, std::forward<Args>( InArgs )... );
    }

    template <typename... Args>
    constexpr void LogTrace( spdlog::format_string_t<Args...> InFormat, Args&&... InArgs )
    {
        spdlog::set_level( spdlog::level::trace );
        spdlog::trace( InFormat, std::forward<Args>( InArgs )... );
    }
} // namespace Common::Loggger

#define LOG_INFO( ... )  Common::Logger::LogInfo( __VA_ARGS__ );
#define LOG_WARN( ... )  Common::Logger::LogWarn( __VA_ARGS__ );
#define LOG_ERROR( ... ) Common::Logger::LogError( __VA_ARGS__ );
#define LOG_TRACE( ... ) Common::Logger::LogTrace( __VA_ARGS__ );
#define LOG_DEBUG( ... ) Common::Logger::LogDebug( __VA_ARGS__ );
