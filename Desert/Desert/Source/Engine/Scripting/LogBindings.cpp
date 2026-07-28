#include "Internal/ScriptRuntime.hpp"

namespace Desert::Scripting
{
    // Logging into the engine Logs panel: log(), Log.info/warn/error.
    void RegisterLogBindings( ScriptEngine::Impl& implRef )
    {
        auto& lua  = implRef.Lua;
        auto* impl = &implRef;
        (void)lua; (void)impl;

        // Route Lua output through the engine logger (shows in the editor's Logs panel).
        lua.set_function( "log", []( const std::string& msg ) { LOG_INFO( "[Lua] {}", msg ); } );

        // Leveled logging: Log.info / Log.warn / Log.error (Logs panel filters by level).
        sol::table logTable = lua.create_named_table( "Log" );
        logTable["info"]    = []( const std::string& msg ) { LOG_INFO( "[Lua] {}", msg ); };
        logTable["warn"]    = []( const std::string& msg ) { LOG_WARN( "[Lua] {}", msg ); };
        logTable["error"]   = []( const std::string& msg ) { LOG_ERROR( "[Lua] {}", msg ); };
    }
} // namespace Desert::Scripting
