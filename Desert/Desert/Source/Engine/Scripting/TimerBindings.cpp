#include "Internal/ScriptRuntime.hpp"

namespace Desert::Scripting
{
    // Timer table: deferred callbacks. Timer.after(seconds, fn) runs fn once after `seconds` of
    // game time (Play only — ticked by ScriptSystem). Ownership follows the (entity, slot) that
    // scheduled it: a reload / destroy cancels the slot's pending timers. A callback may call
    // Timer.after again to re-arm itself (a repeating timer is just recursion).
    void RegisterTimerBindings( ScriptEngine::Impl& implRef )
    {
        auto* impl = &implRef;

        sol::table timer = implRef.Lua.create_named_table( "Timer" );
        timer["after"]   = [impl]( float seconds, sol::protected_function fn )
        {
            if ( !fn.valid() )
                return;
            impl->Timers.push_back( { impl->CurrentOwner, std::max( seconds, 0.0f ), std::move( fn ) } );
        };
    }
} // namespace Desert::Scripting
