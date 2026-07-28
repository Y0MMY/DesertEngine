#include "Internal/ScriptRuntime.hpp"

namespace Desert::Scripting
{
    // Input table: keyboard/mouse state, edge presses, cursor control.
    void RegisterInputBindings( ScriptEngine::Impl& implRef )
    {
        auto& lua  = implRef.Lua;
        auto* impl = &implRef;
        (void)lua; (void)impl;

        // The `Input` table: keyboard state + per-frame mouse delta + cursor control.
        sol::table input    = lua.create_named_table( "Input" );
        input["isKeyDown"]  = []( const std::string& name )
        {
            auto k = KeyFromName( name );
            return k.has_value() && Input::Keyboard::IsKeyPressed( *k );
        };
        // Fires once, on the frame the key goes down (edge). Needs NewInputFrame() once per frame.
        input["wasPressed"] = [impl]( const std::string& name )
        {
            auto k = KeyFromName( name );
            if ( !k.has_value() )
                return false;
            auto it = impl->KeyEdge.find( static_cast<int>( *k ) );
            return it != impl->KeyEdge.end() && it->second;
        };
        input["mouseDelta"]  = [impl]() { return std::make_tuple( impl->MouseDx, impl->MouseDy ); };
        input["lockCursor"]  = [impl]() { impl->CursorLockRequest = true; };  // capture (gameplay look)
        input["showCursor"]  = [impl]() { impl->CursorLockRequest = false; }; // free (click UI)
        // Raw mouse-button held state ("left"/"right"/"middle"). Edge-detect in-script if you need one-shot.
        input["isMouseDown"] = []( const std::string& name )
        {
            Common::MouseButton b = Common::MouseButton::Left;
            if ( name == "right" )
                b = Common::MouseButton::Right;
            else if ( name == "middle" )
                b = Common::MouseButton::Middle;
            return Input::Mouse::Get().IsMouseButtonPressed( b );
        };
    }
} // namespace Desert::Scripting
