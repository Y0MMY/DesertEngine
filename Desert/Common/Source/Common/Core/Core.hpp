#pragma once

#include <memory>
#include "Logger.hpp"
#include "Constants.hpp"
#include <Common/Core/UUID.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Result.hpp>

#define BUILD_ID "v0.1a"

// __VA_ARGS__ expansion to get past MSVC "bug"
#define RE_EXPAND_VARGS( x ) x

#define BIT( x ) ( 1u << x )
#define BIND_FN( fn )                                                                                             \
    [this]( auto&&... args ) -> decltype( auto ) { return this->fn( std::forward<decltype( args )>( args )... ); }

#define RA_HAS_VALUE( var ) var.has_value()

template <typename T>
decltype( auto ) initializeDefaultValue()
{
    if constexpr ( std::is_pointer_v<T> )
    {
        return nullptr;
    }
    else
    {
        return T{};
    }
}

#define RA_GET_VALUE( var ) var.value_or( initializeDefaultValue<decltype( var )::value_type>() )
#define IS_DERIVED_bool( T, X ) ( std::is_base_of<X, T>::value )
#define NO_DISCARD [[nodiscard]]

#define EBABLE_IMGUI 1

#if defined( DESERT_PLATFORM_WINDOWS )
#define DESERT_DEBUG_BREAK __debugbreak()
#elif defined( DESERT_PLATFORM_LINUX )
#include <signal.h>
#define DESERT_DEBUG_BREAK raise( SIGTRAP )
#endif

#define DESERT_VERIFY( cond, ... )                                                                                \
    if ( !( cond ) )                                                                                              \
    {                                                                                                             \
        Common::Logger::LogError( "Verify failed: {} at {}:{}", #cond, __FILE__, __LINE__ );                      \
        __debugbreak();                                                                                           \
    }

#define DESERT_VERIFY_WARN( cond, ... )                                                                           \
    if ( !( cond ) )                                                                                              \
    {                                                                                                             \
        Common::Logger::LogWarn( "Verify failed: {} at {}:{}", #cond, __FILE__, __LINE__ );                       \
    }

// #define MAKE_SHARED_OBJECT( type, value ) Memory::Shared<type>::Create( value );

namespace Common
{
    template <typename T>
    using Unique = std::unique_ptr<T>;
}

namespace Common
{
    using serialized_str = std::string;
    class UUID;
    using AssetHandle = UUID;
} // namespace Common

#define REGISTER_EVENT( instance, method )                                                                        \
    Engine::Application::GetInstance().s_RegisteredEvents.push_back( [instance]( Common::Event& e )               \
                                                                     { instance->method( e ); } )