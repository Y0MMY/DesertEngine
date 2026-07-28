#include "Internal/ScriptRuntime.hpp"

namespace Desert::Scripting
{
    ScriptEngine::ScriptEngine( Core::Scene* scene, Assets::AssetManager* assetManager )
         : m_Impl( std::make_unique<Impl>() )
    {
        m_Impl->Scene  = scene;
        m_Impl->Assets = assetManager;

        m_Impl->Lua.open_libraries( sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table,
                                    sol::lib::os );

        // Modular bindings: the core owns the VM; every domain registers its own API from its
        // own translation unit (see Internal/ScriptRuntime.hpp for the architecture note).
        RegisterLogBindings( *m_Impl );
        RegisterEntityCoreBindings( *m_Impl );
        RegisterCharacterBindings( *m_Impl );
        RegisterMaterialBindings( *m_Impl );
        RegisterInputBindings( *m_Impl );
        RegisterTimerBindings( *m_Impl );
        RegisterWorldBindings( *m_Impl );
        RegisterReflectionBindings( *m_Impl ); // after EntityCore: extends the Entity usertype
    }

    ScriptEngine::~ScriptEngine() = default;

    Common::BoolResultStr ScriptEngine::RunString( const std::string& code )
    {
        sol::protected_function_result result = m_Impl->Lua.safe_script( code, sol::script_pass_on_error );
        if ( !result.valid() )
        {
            sol::error err = result;
            return Common::MakeError( err.what() );
        }
        return BOOLSUCCESS;
    }

    // Returns the env for (entity, slot), or nullptr if not loaded. Grows the slot vector on demand when
    // `create` is set (used by LoadEntityScript so slots can be (re)loaded in any order).
    static sol::environment* SlotEnv( EnvMap& envs, uint32_t entity, uint32_t slot, bool create )
    {
        auto it = envs.find( entity );
        if ( it == envs.end() )
        {
            if ( !create )
                return nullptr;
            it = envs.emplace( entity, std::vector<sol::environment>{} ).first;
        }
        if ( slot >= it->second.size() )
        {
            if ( !create )
                return nullptr;
            it->second.resize( slot + 1, sol::environment{} );
        }
        return &it->second[slot];
    }

    Common::BoolResultStr ScriptEngine::LoadEntityScript( uint32_t entity, uint32_t slot,
                                                          const std::string& path )
    {
        sol::environment env( m_Impl->Lua, sol::create, m_Impl->Lua.globals() );
        env["self"] = m_Impl->MakeEntity( static_cast<entt::entity>( entity ) );

        sol::protected_function_result r =
             m_Impl->Lua.safe_script_file( path, env, sol::script_pass_on_error );
        if ( !r.valid() )
        {
            sol::error err = r;
            return Common::MakeError( err.what() );
        }

        *SlotEnv( m_Impl->Envs, entity, slot, /*create*/ true ) = std::move( env );
        const uint64_t key = Impl::SlotKey( entity, slot );
        m_Impl->LastUpdateError.erase( key ); // fresh env -> fresh error state
        // Fresh env -> the OLD env's pending timers must not fire into it (hot-reload safety).
        std::erase_if( m_Impl->Timers, [key]( const Impl::PendingTimer& t ) { return t.Owner == key; } );
        return BOOLSUCCESS;
    }

    void ScriptEngine::CallStart( uint32_t entity, uint32_t slot )
    {
        sol::environment* env = SlotEnv( m_Impl->Envs, entity, slot, false );
        if ( !env )
            return;
        sol::protected_function fn = ( *env )["OnStart"];
        if ( !fn.valid() )
            return;
        m_Impl->CurrentOwner             = Impl::SlotKey( entity, slot ); // Timer.after ownership
        sol::protected_function_result r = fn();
        if ( !r.valid() )
        {
            sol::error err = r;
            LOG_ERROR( "[Lua] OnStart error: {}", err.what() );
        }
    }

    void ScriptEngine::CallUpdate( uint32_t entity, uint32_t slot, float dt )
    {
        sol::environment* env = SlotEnv( m_Impl->Envs, entity, slot, false );
        if ( !env )
            return;
        sol::protected_function fn = ( *env )["OnUpdate"];
        if ( !fn.valid() )
            return;
        const uint64_t key    = Impl::SlotKey( entity, slot );
        m_Impl->CurrentOwner  = key; // Timer.after ownership
        sol::protected_function_result r = fn( dt );
        if ( !r.valid() )
        {
            sol::error        err  = r;
            const std::string what = err.what();
            // OnUpdate runs every frame — report a given error once, not 60x/sec.
            if ( m_Impl->LastUpdateError[key] != what )
            {
                m_Impl->LastUpdateError[key] = what;
                LOG_ERROR( "[Lua] OnUpdate error: {}", what );
            }
        }
        else
        {
            m_Impl->LastUpdateError.erase( key );
        }
    }

    void ScriptEngine::ApplyProperties( uint32_t entity, uint32_t slot,
                                        const std::vector<ScriptProperty>& props )
    {
        sol::environment* env = SlotEnv( m_Impl->Envs, entity, slot, false );
        if ( !env )
            return;

        // Ensure the env has a `Properties` table (the script usually declares one, but be safe).
        sol::object existing = ( *env )["Properties"];
        if ( !existing.is<sol::table>() )
            ( *env )["Properties"] = m_Impl->Lua.create_table();
        sol::table t = ( *env )["Properties"];

        for ( const auto& p : props )
        {
            switch ( p.Type )
            {
                case PropertyType::Number: t[p.Name] = p.Number; break;
                case PropertyType::Bool:   t[p.Name] = p.Bool; break;
                case PropertyType::String: t[p.Name] = p.Str; break;
            }
        }
    }

    void ScriptEngine::Release( uint32_t entity )
    {
        m_Impl->Envs.erase( entity );
        std::erase_if( m_Impl->Timers, [entity]( const Impl::PendingTimer& t )
                       { return static_cast<uint32_t>( t.Owner >> 32 ) == entity; } );
    }

    void ScriptEngine::TrimSlots( uint32_t entity, uint32_t count )
    {
        auto it = m_Impl->Envs.find( entity );
        if ( it != m_Impl->Envs.end() && it->second.size() > count )
            it->second.resize( count );
        std::erase_if( m_Impl->Timers, [entity, count]( const Impl::PendingTimer& t )
                       { return static_cast<uint32_t>( t.Owner >> 32 ) == entity &&
                                static_cast<uint32_t>( t.Owner & 0xFFFFFFFFu ) >= count; } );
    }

    void ScriptEngine::TickTimers( float dt )
    {
        // Two-phase so a firing callback can safely schedule new timers (Timer.after re-arm):
        // extract everything due first, then invoke — new pushes land in m_Impl->Timers untouched.
        std::vector<Impl::PendingTimer> due;
        for ( auto it = m_Impl->Timers.begin(); it != m_Impl->Timers.end(); )
        {
            it->Remaining -= dt;
            if ( it->Remaining <= 0.0f )
            {
                due.push_back( std::move( *it ) );
                it = m_Impl->Timers.erase( it );
            }
            else
                ++it;
        }
        for ( auto& t : due )
        {
            m_Impl->CurrentOwner             = t.Owner; // a re-arm inherits the same (entity, slot)
            sol::protected_function_result r = t.Fn();
            if ( !r.valid() )
            {
                sol::error err = r;
                LOG_ERROR( "[Lua] Timer.after error: {}", err.what() );
            }
        }
    }

    std::vector<ScriptProperty> ReadScriptProperties( const std::string& path )
    {
        std::vector<ScriptProperty> out;

        // Throwaway state: we only need to read the top-level `Properties` table. base lib is enough — the
        // file's top level just sets locals / Properties / defines functions (no engine calls at load time).
        sol::state lua;
        lua.open_libraries( sol::lib::base, sol::lib::math );
        sol::protected_function_result r = lua.safe_script_file( path, sol::script_pass_on_error );
        if ( !r.valid() )
            return out;

        sol::object propsObj = lua["Properties"];
        if ( !propsObj.is<sol::table>() )
            return out;

        sol::table props = propsObj.as<sol::table>();
        for ( const auto& kv : props )
        {
            if ( kv.first.get_type() != sol::type::string )
                continue;
            const std::string name = kv.first.as<std::string>();
            const sol::object value = kv.second;

            ScriptProperty p;
            p.Name = name;
            if ( value.is<bool>() ) // check bool BEFORE number (distinct Lua types)
            {
                p.Type = PropertyType::Bool;
                p.Bool = value.as<bool>();
            }
            else if ( value.is<double>() )
            {
                p.Type   = PropertyType::Number;
                p.Number = value.as<double>();
            }
            else if ( value.is<std::string>() )
            {
                p.Type = PropertyType::String;
                p.Str  = value.as<std::string>();
            }
            else
            {
                continue; // unsupported type (table/function/...)
            }
            out.push_back( p );
        }
        return out;
    }

    void ScriptEngine::SetFrameMouseDelta( float dx, float dy )
    {
        m_Impl->MouseDx = dx;
        m_Impl->MouseDy = dy;
    }

    void ScriptEngine::NewInputFrame()
    {
        for ( Common::KeyCode key : TrackedKeys() )
        {
            const int  id   = static_cast<int>( key );
            const bool down = Input::Keyboard::IsKeyPressed( key );
            const bool prev = m_Impl->KeyDownPrev[id];
            m_Impl->KeyEdge[id]     = down && !prev; // rising edge
            m_Impl->KeyDownPrev[id] = down;
        }
    }

    std::optional<bool> ScriptEngine::ConsumeCursorLockRequest()
    {
        std::optional<bool> req = m_Impl->CursorLockRequest;
        m_Impl->CursorLockRequest.reset();
        return req;
    }
} // namespace Desert::Scripting
