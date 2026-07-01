#pragma once

#include <Engine/ECS/System/System.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Core/Input.hpp>
#include <Engine/Scripting/ScriptEngine.hpp>

#include <Common/Core/KeyCodes.hpp>
#include <Common/Core/Logger.hpp>

#include <glm/glm.hpp>

#include <cstdint>

namespace Desert::ECS
{
    // Runs entity Lua scripts (Play only): loads each ScriptComponent's file once (OnStart) then calls
    // OnUpdate(dt) per frame. Also owns the per-frame INPUT plumbing the scripts read — cursor capture and the
    // mouse delta — which used to live in PhysicsECSSystem (it now only executes the move intent the scripts
    // set). Left Alt toggles the cursor free so you can click Stop / the editor UI.
    class ScriptSystem final : public System
    {
    public:
        explicit ScriptSystem( Core::Scene* scene, Assets::AssetManager* assetManager = nullptr )
             : m_Scene( scene ), m_Engine( scene, assetManager )
        {
        }

        ~ScriptSystem() override
        {
            // Don't leave a dangling on_destroy listener pointing at this (freed) system.
            if ( m_HookedRegistry )
                m_HookedRegistry->on_destroy<ScriptComponent>().disconnect( this );
        }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer&,
                     const Common::Timestep& ts ) override
        {
            // Release an entity's Lua envs the instant its ScriptComponent dies (entity destroyed, component
            // removed, or scene cleared). Without this the env map leaks AND — since entt recycles entity ids
            // — a NEW entity could inherit a destroyed one's stale env. Event-driven (entt keeps the connection
            // across registry.clear(), so wire it once; re-arm only if the registry object itself changes).
            EnsureDestroyHook( registry );

            using SceneState = Core::Scene::SceneState;
            const bool playing = m_Scene && m_Scene->GetState() == SceneState::Play;

            if ( !playing )
            {
                if ( m_CursorLocked ) // give the cursor back when not playing
                {
                    Input::Mouse::Get().SetCursorMode( Input::MouseState::Visible );
                    m_CursorLocked = false;
                }
                m_LookSuspended = false; // next Play starts captured again
                return;
            }

            // Advance edge-detection state so Input.wasPressed() fires exactly on the press transition.
            m_Engine.NewInputFrame();

            // ---- Per-frame input plumbing exposed to scripts (Input.mouseDelta()) ----
            // ESCAPE TOGGLES the cursor free/captured (edge-detected) — so you can always free it to click
            // Stop / the UI. (Left Alt is avoided: on Windows it grabs the window's system menu and is
            // unreliable in GLFW.) Captured = smooth unbounded look.
            const bool toggleDown = Input::Keyboard::IsKeyPressed( Common::KeyCode::Escape );
            if ( toggleDown && !m_AltPrev )
                m_LookSuspended = !m_LookSuspended;
            m_AltPrev = toggleDown;

            const bool wantLock = !m_LookSuspended;
            bool       toggled  = false;
            if ( wantLock != m_CursorLocked )
            {
                Input::Mouse::Get().SetCursorMode( wantLock ? Input::MouseState::Locked
                                                            : Input::MouseState::Visible );
                m_CursorLocked = wantLock;
                toggled        = true; // GLFW recenters on lock → skip this frame's delta to avoid a jump
            }

            const auto      mp = Input::Mouse::Get().GetMousePosition();
            const glm::vec2 mouseNow( mp.first, mp.second );
            const glm::vec2 delta = ( wantLock && !toggled ) ? ( mouseNow - m_LastMouse ) : glm::vec2( 0.0f );
            m_LastMouse           = mouseNow;
            m_Engine.SetFrameMouseDelta( delta.x, delta.y );

            // ---- Run the scripts (each entity may run several script SLOTS, like UE ActorComponents) ----
            auto view = registry.view<ScriptComponent>();
            for ( auto entity : view )
            {
                auto&          sc = view.get<ScriptComponent>( entity );
                const uint32_t id = static_cast<uint32_t>( entity );

                // Keep the engine's per-entity env list in lockstep with the component's slot count (a removed
                // slot drops its env, so indices stay aligned).
                m_Engine.TrimSlots( id, static_cast<uint32_t>( sc.Scripts.size() ) );

                for ( uint32_t slot = 0; slot < sc.Scripts.size(); ++slot )
                {
                    auto& script = sc.Scripts[slot];
                    if ( script.ScriptPath.empty() )
                        continue;

                    if ( !script.Started )
                    {
                        script.Started = true; // set first so a load error doesn't retry-spam every frame
                        auto loaded    = m_Engine.LoadEntityScript( id, slot, script.ScriptPath );
                        if ( !loaded )
                        {
                            LOG_ERROR( "[Script] failed to load '{}': {}", script.ScriptPath,
                                       loaded.GetError() );
                            continue;
                        }
                        m_Engine.ApplyProperties( id, slot, script.Properties ); // editor overrides -> env
                        m_Engine.CallStart( id, slot );
                    }
                    // Re-apply every frame so editing a property in Details updates the running script LIVE.
                    m_Engine.ApplyProperties( id, slot, script.Properties );
                    m_Engine.CallUpdate( id, slot, ts.GetSeconds() );
                }
            }

            // A script may have requested cursor lock/unlock (Input.lockCursor/showCursor). Apply it so it
            // cooperates with the Escape toggle (also keeps m_LookSuspended in sync for the next Escape press).
            if ( auto req = m_Engine.ConsumeCursorLockRequest() )
            {
                m_LookSuspended = !*req;
                if ( *req != m_CursorLocked )
                {
                    Input::Mouse::Get().SetCursorMode( *req ? Input::MouseState::Locked
                                                            : Input::MouseState::Visible );
                    m_CursorLocked = *req;
                }
            }
        }

    private:
        // Connects the on_destroy<ScriptComponent> listener to `registry` once (re-arms if the registry object
        // changes, e.g. a brand-new scene). Cheap no-op on every subsequent frame.
        void EnsureDestroyHook( entt::registry& registry )
        {
            if ( m_HookedRegistry == &registry )
                return;
            if ( m_HookedRegistry )
                m_HookedRegistry->on_destroy<ScriptComponent>().disconnect( this );
            registry.on_destroy<ScriptComponent>().connect<&ScriptSystem::OnScriptComponentDestroyed>( this );
            m_HookedRegistry = &registry;
        }

        void OnScriptComponentDestroyed( entt::registry&, entt::entity entity )
        {
            m_Engine.Release( static_cast<uint32_t>( entity ) );
        }

    private:
        Core::Scene*            m_Scene = nullptr;
        Scripting::ScriptEngine m_Engine;
        entt::registry*         m_HookedRegistry = nullptr; // registry our on_destroy listener is wired to

        glm::vec2 m_LastMouse{ 0.0f, 0.0f };
        bool      m_CursorLocked  = false;
        bool      m_LookSuspended = false;
        bool      m_AltPrev       = false;
    };
} // namespace Desert::ECS
