#pragma once

#include <spdlog/spdlog.h>

namespace Common::Logger
{
    inline void LogInit()
    {
        spdlog::set_pattern( "%^[%T][Desert]: %v%$" );
    }

    template <typename... Args>
    constexpr void LogDebug( const std::string_view inMessage, Args&&... InArgs )
    {
        spdlog::set_level( spdlog::level::debug );
        std::string message = fmt::vformat( inMessage, fmt::make_format_args( InArgs... ) );
        spdlog::debug( message );
    }

    template <typename... Args>
    constexpr void LogInfo( const std::string_view inMessage, Args&&... InArgs )
    {
        spdlog::set_level( spdlog::level::info );
        std::string message = fmt::vformat( inMessage, fmt::make_format_args( InArgs... ) );
        spdlog::info( message );
    }

    template <typename... Args>
    constexpr void LogWarn( const std::string_view inMessage, Args&&... InArgs )
    {
        spdlog::set_level( spdlog::level::warn );
        std::string message = fmt::vformat( inMessage, fmt::make_format_args( InArgs... ) );
        spdlog::warn( message );
    }

    template <typename... Args>
    constexpr void LogError( const std::string_view inMessage, Args&&... InArgs )
    {
        spdlog::set_level( spdlog::level::err );
        std::string message = fmt::vformat( inMessage, fmt::make_format_args( InArgs... ) );
        spdlog::error( message );
    }

    template <typename... Args>
    constexpr void LogTrace( const std::string_view inMessage, Args&&... InArgs )
    {
        spdlog::set_level( spdlog::level::trace );
        std::string message = fmt::vformat( inMessage, fmt::make_format_args( InArgs... ) );
        spdlog::trace( message );
    }
} // namespace Common::Logger

#define LOG_INFO( ... ) Common::Logger::LogInfo( __VA_ARGS__ );
#define LOG_WARN( ... ) Common::Logger::LogWarn( __VA_ARGS__ );
#define LOG_ERROR( ... ) Common::Logger::LogError( __VA_ARGS__ );
#define LOG_TRACE( ... ) Common::Logger::LogTrace( __VA_ARGS__ );
#define LOG_DEBUG( ... ) Common::Logger::LogDebug( __VA_ARGS__ );
