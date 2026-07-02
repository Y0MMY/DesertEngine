#include "ScriptEngine.hpp"

#include <Common/Core/Core.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Core/KeyCodes.hpp>
#include <Common/Core/Math/Ray.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Core/Input.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Geometry/PrimitiveType.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Prefab/PrefabAsset.hpp>

#include <glm/glm.hpp>

// sol2 + Lua are pulled in ONLY here (PIMPL) — heavy headers stay out of the rest of the engine.
#include <sol/sol.hpp>

#include <optional>
#include <unordered_map>

namespace Desert::Scripting
{
    // The per-entity Lua environment map type: entt handle -> one sandbox env PER SCRIPT SLOT. Declared here
    // so the bound ScriptEntity can reach sibling entities' scripts for cross-entity messaging (entity:call,
    // which broadcasts to every slot of the target).
    using EnvMap = std::unordered_map<uint32_t, std::vector<sol::environment>>;

    namespace
    {
        // Maps script-friendly key names to engine key codes (the ones gameplay scripts use).
        std::optional<Common::KeyCode> KeyFromName( const std::string& name )
        {
            using K = Common::KeyCode;
            if ( name == "W" ) return K::W;
            if ( name == "A" ) return K::A;
            if ( name == "S" ) return K::S;
            if ( name == "D" ) return K::D;
            if ( name == "Q" ) return K::Q;
            if ( name == "E" ) return K::E;
            if ( name == "F" ) return K::F;
            if ( name == "R" ) return K::R;
            if ( name == "Space" ) return K::Space;
            if ( name == "Shift" || name == "LeftShift" ) return K::LeftShift;
            if ( name == "Ctrl" || name == "LeftControl" ) return K::LeftControl;
            return std::nullopt;
        }

        // The keys Input.wasPressed() edge-tracks each frame (NewInputFrame). Keep in sync with KeyFromName.
        constexpr Common::KeyCode kTrackedKeys[] = {
            Common::KeyCode::W,     Common::KeyCode::A,           Common::KeyCode::S,
            Common::KeyCode::D,     Common::KeyCode::Q,           Common::KeyCode::E,
            Common::KeyCode::F,     Common::KeyCode::R,           Common::KeyCode::Space,
            Common::KeyCode::LeftShift, Common::KeyCode::LeftControl };

        // Generic entity handle bound to Lua. The SAME usertype is `self`, World.find/spawn/raycast results, etc.
        // Transform/name/destroy work on any entity; the character methods (move/jump/look) no-op without a
        // CharacterControllerComponent. `scene`+`envs` let it reach components and sibling scripts.
        struct ScriptEntity
        {
            entt::entity handle = entt::null;
            Core::Scene* scene  = nullptr;
            EnvMap*      envs   = nullptr;

            entt::registry& Reg() const { return scene->GetRegistry(); }

            bool Valid() const
            {
                return scene && handle != entt::null && Reg().valid( handle );
            }

            std::string Name() const
            {
                if ( Valid() && Reg().has<ECS::TagComponent>( handle ) )
                    return Reg().get<ECS::TagComponent>( handle ).Tag;
                return {};
            }

            ECS::TransformComponent* Transform() const
            {
                if ( Valid() && Reg().has<ECS::TransformComponent>( handle ) )
                    return &Reg().get<ECS::TransformComponent>( handle );
                return nullptr;
            }

            std::tuple<float, float, float> GetPosition() const
            {
                if ( auto* t = Transform() )
                    return { t->Translation.x, t->Translation.y, t->Translation.z };
                return { 0.0f, 0.0f, 0.0f };
            }
            void SetPosition( float x, float y, float z )
            {
                if ( auto* t = Transform() )
                    t->Translation = { x, y, z };
            }
            void Translate( float dx, float dy, float dz )
            {
                if ( auto* t = Transform() )
                    t->Translation += glm::vec3( dx, dy, dz );
            }
            std::tuple<float, float, float> GetRotation() const // euler radians
            {
                if ( auto* t = Transform() )
                    return { t->Rotation.x, t->Rotation.y, t->Rotation.z };
                return { 0.0f, 0.0f, 0.0f };
            }
            void SetRotation( float x, float y, float z )
            {
                if ( auto* t = Transform() )
                    t->Rotation = { x, y, z };
            }

            // Remove this entity (and its subtree) from the scene. Releases its script env too.
            void Destroy()
            {
                if ( !Valid() )
                    return;
                if ( envs )
                    envs->erase( static_cast<uint32_t>( handle ) );
                scene->DestroyEntity( ECS::Entity{ handle, Reg() } );
                handle = entt::null;
            }

            // Cross-entity message: invoke a named function in THIS entity's scripts, passing args (the caller
            // usually sends `self`). BROADCASTS to every script slot that defines `fn` (an entity may run many
            // behaviors). The mechanism behind OnInteract: the instigator raycasts then target:call().
            void Call( const std::string& fn, sol::variadic_args va )
            {
                if ( !envs )
                    return;
                auto it = envs->find( static_cast<uint32_t>( handle ) );
                if ( it == envs->end() )
                    return;
                for ( sol::environment& env : it->second )
                {
                    sol::protected_function f = env[fn];
                    if ( !f.valid() )
                        continue;
                    sol::protected_function_result r = f( sol::as_args( va ) );
                    if ( !r.valid() )
                    {
                        sol::error err = r;
                        LOG_ERROR( "[Lua] {} error: {}", fn, err.what() );
                    }
                }
            }

            // Socket-attach this entity to a bone of `target` (UE-style): adds a SocketAttachmentComponent so
            // AttachmentSystem makes us follow that bone each frame. e.g. weapon:attachTo(player, "hand_r").
            void AttachTo( ScriptEntity target, const std::string& bone )
            {
                if ( !Valid() || !target.Valid() )
                    return;
                Common::UUID targetId;
                if ( target.Reg().has<ECS::UUIDComponent>( target.handle ) )
                    targetId = target.Reg().get<ECS::UUIDComponent>( target.handle ).UUID;
                if ( !Reg().has<ECS::SocketAttachmentComponent>( handle ) )
                    Reg().emplace<ECS::SocketAttachmentComponent>( handle );
                auto& sa    = Reg().get<ECS::SocketAttachmentComponent>( handle );
                sa.Target   = targetId;
                sa.BoneName = bone;
            }

            void Detach()
            {
                if ( Valid() && Reg().has<ECS::SocketAttachmentComponent>( handle ) )
                    Reg().remove<ECS::SocketAttachmentComponent>( handle );
            }

            ECS::CharacterControllerComponent* Character() const
            {
                if ( Valid() && Reg().has<ECS::CharacterControllerComponent>( handle ) )
                    return &Reg().get<ECS::CharacterControllerComponent>( handle );
                return nullptr;
            }

            // forward = W/S axis, right = D/A axis (each -1..1), speed in m/s.
            void Move( float forward, float right, float speed )
            {
                if ( auto* cc = Character() )
                {
                    cc->MoveInput    = { right, forward };
                    cc->DesiredSpeed = speed;
                }
            }

            void Jump( float strength )
            {
                if ( auto* cc = Character() )
                {
                    cc->JumpRequested = true;
                    cc->JumpStrength  = strength;
                }
            }

            bool IsOnGround() const
            {
                auto* cc = Character();
                return cc ? cc->OnGround : false;
            }

            // Swimming (buoyancy): the script toggles this when the body crosses the water surface. `vertical`
            // is the swim up/down intent (+1 up, -1 down) applied while swimming.
            void SetSwimming( bool on )
            {
                if ( auto* cc = Character() )
                    cc->Swimming = on;
            }
            void Swim( float vertical )
            {
                if ( auto* cc = Character() )
                    cc->SwimVertical = vertical;
            }
            bool IsSwimming() const
            {
                auto* cc = Character();
                return cc ? cc->Swimming : false;
            }

            // Yaw turns the whole entity (body + child camera follow through the hierarchy).
            void AddYaw( float radians )
            {
                if ( auto* t = Transform() )
                    t->Rotation.y += radians;
            }

            // Pitch tilts the child camera only (look up/down), clamped to avoid flipping over.
            void AddCameraPitch( float radians )
            {
                if ( !Valid() || !Reg().has<ECS::RelationshipComponent>( handle ) )
                    return;
                auto& reg = Reg();
                for ( entt::entity child : reg.get<ECS::RelationshipComponent>( handle ).Children )
                {
                    if ( reg.has<ECS::CameraComponent>( child ) && reg.has<ECS::TransformComponent>( child ) )
                    {
                        auto& rot = reg.get<ECS::TransformComponent>( child ).Rotation;
                        rot.x     = glm::clamp( rot.x + radians, glm::radians( -85.0f ), glm::radians( 85.0f ) );
                        return;
                    }
                }
            }
        };
    } // namespace

    struct ScriptEngine::Impl
    {
        sol::state             Lua;
        Core::Scene*           Scene  = nullptr;
        Assets::AssetManager*  Assets = nullptr;
        EnvMap                 Envs;
        float                  MouseDx = 0.0f;
        float                  MouseDy = 0.0f;

        // Input.wasPressed() edge state (down this frame & up last frame), refreshed by NewInputFrame().
        std::unordered_map<int, bool> KeyDownPrev;
        std::unordered_map<int, bool> KeyEdge;

        std::optional<bool> CursorLockRequest; // set by Input.lockCursor()/showCursor(), consumed by ScriptSystem

        // Build a Lua-bound handle for an entt entity (carries the scene + env map so its methods work).
        ScriptEntity MakeEntity( entt::entity h ) { return ScriptEntity{ h, Scene, &Envs }; }
    };

    ScriptEngine::ScriptEngine( Core::Scene* scene, Assets::AssetManager* assetManager )
         : m_Impl( std::make_unique<Impl>() )
    {
        m_Impl->Scene  = scene;
        m_Impl->Assets = assetManager;
        Impl* impl     = m_Impl.get();

        auto& lua = m_Impl->Lua;
        lua.open_libraries( sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::os );

        // Route Lua output through the engine logger.
        lua.set_function( "log", []( const std::string& msg ) { LOG_INFO( "[Lua] {}", msg ); } );

        // The generic entity API (used for `self`, World.find/spawn/raycast results, etc.).
        lua.new_usertype<ScriptEntity>(
             "Entity",
             // identity / lifetime
             "valid", &ScriptEntity::Valid, "name", &ScriptEntity::Name, "destroy", &ScriptEntity::Destroy,
             "call", &ScriptEntity::Call, "attachTo", &ScriptEntity::AttachTo, "detach", &ScriptEntity::Detach,
             // transform
             "getPosition", &ScriptEntity::GetPosition, "setPosition", &ScriptEntity::SetPosition, "translate",
             &ScriptEntity::Translate, "getRotation", &ScriptEntity::GetRotation, "setRotation",
             &ScriptEntity::SetRotation,
             // character controller (no-op without the component)
             "move", &ScriptEntity::Move, "jump", &ScriptEntity::Jump, "isOnGround", &ScriptEntity::IsOnGround,
             "addYaw", &ScriptEntity::AddYaw, "addCameraPitch", &ScriptEntity::AddCameraPitch,
             // swimming
             "setSwimming", &ScriptEntity::SetSwimming, "swim", &ScriptEntity::Swim, "isSwimming",
             &ScriptEntity::IsSwimming );

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

        // The `World` table: scene-wide queries the engine performs on the script's behalf.
        sol::table world = lua.create_named_table( "World" );

        // Find the first entity whose name (TagComponent) matches. Returns an Entity (check :valid()).
        world["find"] = [impl]( const std::string& name ) -> ScriptEntity
        {
            auto& reg  = impl->Scene->GetRegistry();
            auto  view = reg.view<ECS::TagComponent>();
            for ( auto e : view )
            {
                if ( view.get<ECS::TagComponent>( e ).Tag == name )
                    return impl->MakeEntity( e );
            }
            return impl->MakeEntity( entt::null );
        };

        // Cast a ray through the scene's static meshes. Returns a table:
        //   { hit=bool, entity=Entity, x,y,z (point), nx,ny,nz (normal), dist=number }
        world["raycast"] = [impl]( float ox, float oy, float oz, float dx, float dy, float dz,
                                   sol::optional<float> maxDist ) -> sol::table
        {
            sol::table       result = impl->Lua.create_table();
            Common::Math::Ray ray( glm::vec3( ox, oy, oz ), glm::vec3( dx, dy, dz ) );
            Core::RaycastHit  hit;
            const bool        ok = impl->Scene->Raycast( ray, hit );
            const bool inRange   = ok && ( !maxDist || hit.Distance <= *maxDist );
            result["hit"]        = inRange;
            if ( inRange )
            {
                entt::entity h = entt::null;
                if ( auto found = impl->Scene->FindEntityByID( hit.Entity ) )
                    h = found->get().GetHandle();
                result["entity"] = impl->MakeEntity( h );
                result["x"]      = hit.Point.x;
                result["y"]      = hit.Point.y;
                result["z"]      = hit.Point.z;
                result["nx"]     = hit.Normal.x;
                result["ny"]     = hit.Normal.y;
                result["nz"]     = hit.Normal.z;
                result["dist"]   = hit.Distance;
            }
            return result;
        };

        // The active camera's eye + forward (origin, dir) — the natural ray for "what am I looking at".
        world["cameraRay"] = [impl]() -> std::tuple<float, float, float, float, float, float>
        {
            if ( auto cam = impl->Scene->GetActiveCamera() )
            {
                // Derive eye + forward from the view matrix (works for any Camera subtype): the inverse view's
                // translation is the eye, and its -Z column is the world-space forward direction.
                const glm::mat4 inv = glm::inverse( cam->GetViewMatrix() );
                const glm::vec3 o   = glm::vec3( inv[3] );
                const glm::vec3 d   = -glm::normalize( glm::vec3( inv[2] ) );
                return { o.x, o.y, o.z, d.x, d.y, d.z };
            }
            return { 0, 0, 0, 0, 0, -1 };
        };

        // Instantiate a prefab at a world position. Returns the new root Entity (check :valid()).
        world["spawn"] = [impl]( const std::string& prefabPath, float x, float y,
                                 float z ) -> ScriptEntity
        {
            if ( !impl->Assets )
            {
                LOG_ERROR( "[Lua] World.spawn: no AssetManager bound" );
                return impl->MakeEntity( entt::null );
            }
            auto prefab = impl->Assets->FindByPath<Assets::PrefabAsset>( prefabPath );
            if ( !prefab )
                prefab = impl->Assets->CreateAsset<Assets::PrefabAsset>( Assets::AssetPriority::High,
                                                                         prefabPath );
            if ( !prefab )
            {
                LOG_ERROR( "[Lua] World.spawn: prefab not found '{}'", prefabPath );
                return impl->MakeEntity( entt::null );
            }
            const glm::vec3 pos( x, y, z );
            ECS::Entity     root = prefab->Instantiate( impl->Scene, *impl->Assets, &pos );
            return impl->MakeEntity( root ? root.GetHandle() : entt::null );
        };

        // Spawn a small solid-colour marker sphere at a world position (e.g. a bullet-impact "red spot").
        // Uses the data-driven Unlit shader so the colour shows regardless of scene lighting. Returns the
        // new Entity (call :destroy() to remove it, e.g. after a lifetime).
        world["spawnMarker"] = [impl]( float x, float y, float z, float scale, float r, float g,
                                       float b ) -> ScriptEntity
        {
            ECS::Entity e = impl->Scene->CreateNewEntity( "Marker" );
            e.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Sphere;

            auto& t       = e.GetComponent<ECS::TransformComponent>();
            t.Translation = glm::vec3( x, y, z );
            t.Scale       = glm::vec3( scale <= 0.0f ? 0.15f : scale );

            auto& mc      = e.AddComponent<ECS::MaterialComponent>();
            mc.ShaderName = "Unlit";
            mc.Params.push_back( ECS::MaterialParamOverride{ "Color", glm::vec4( r, g, b, 1.0f ) } );

            return impl->MakeEntity( e.GetHandle() );
        };

        // Water: the scene-global water level (SceneSettings) the swim logic reads, + a helper to drop a
        // visible water plane at a level.
        world["waterLevel"]   = [impl]() { return impl->Scene->GetSettings().WaterLevel; };
        world["waterEnabled"] = [impl]() { return impl->Scene->GetSettings().WaterEnabled; };
        world["spawnWater"]   = [impl]( float level, float size ) -> ScriptEntity
        {
            ECS::Entity e = impl->Scene->CreateNewEntity( "Water" );
            e.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Plane;

            auto& t       = e.GetComponent<ECS::TransformComponent>();
            t.Translation = glm::vec3( 0.0f, level, 0.0f );
            t.Scale       = glm::vec3( size <= 0.0f ? 100.0f : size, 1.0f, size <= 0.0f ? 100.0f : size );

            auto& mc      = e.AddComponent<ECS::MaterialComponent>();
            mc.ShaderName = "Unlit";
            mc.Params.push_back( ECS::MaterialParamOverride{ "Color", glm::vec4( 0.10f, 0.35f, 0.60f, 0.6f ) } );

            return impl->MakeEntity( e.GetHandle() );
        };
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
        sol::protected_function_result r = fn( dt );
        if ( !r.valid() )
        {
            sol::error err = r;
            LOG_ERROR( "[Lua] OnUpdate error: {}", err.what() );
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
    }

    void ScriptEngine::TrimSlots( uint32_t entity, uint32_t count )
    {
        auto it = m_Impl->Envs.find( entity );
        if ( it != m_Impl->Envs.end() && it->second.size() > count )
            it->second.resize( count );
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
        for ( Common::KeyCode key : kTrackedKeys )
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
