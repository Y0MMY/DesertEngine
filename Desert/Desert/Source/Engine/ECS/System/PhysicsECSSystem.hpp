#pragma once

#include <Engine/ECS/System/System.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Physics/PhysicsWorld.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Core/Input.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Geometry/ProceduralCharacterFactory.hpp>
#include <Engine/Animation/ProceduralCharacterAnimations.hpp>
#include <Engine/Animation/Animator.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/KeyCodes.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>

namespace Desert::ECS
{
    // Drives the Jolt PhysicsWorld from ECS data. Lifecycle is gated on the scene's play state:
    //   - Edit:   no simulation. Any live world is torn down so authored transforms stay freely
    //             editable (you can move/select physics bodies in the viewport).
    //   - Play:   bodies are (lazily) created from the authored transform of every entity that has a
    //             RigidBody + Collider, the simulation steps, and the resulting pose is written back
    //             into TransformComponent (so the mesh renderer draws bodies where physics puts them).
    //   - Paused: bodies persist but the simulation is frozen (no step, no writeback).
    //
    // The editor snapshots the scene on Play and restores it on Stop, so the world is rebuilt fresh
    // each Play (RuntimeBody handles come back as kInvalidBody after the snapshot is reloaded).
    class PhysicsECSSystem final : public System
    {
    public:
        explicit PhysicsECSSystem( Core::Scene* scene ) : m_Scene( scene )
        {
        }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer&,
                     const Common::Timestep& ts ) override
        {
            using SceneState = Core::Scene::SceneState;

            const SceneState state   = m_Scene ? m_Scene->GetState() : SceneState::Edit;
            const bool       playing = ( state == SceneState::Play );
            const bool       active  = ( state == SceneState::Play || state == SceneState::Paused );

            if ( !active )
            {
                // Back to Edit (or never played): drop the live simulation. The scene is re-deserialized
                // from the play snapshot on Stop, so the entities' RuntimeBody handles reset themselves.
                if ( m_World )
                {
                    m_World->Shutdown();
                    m_World.reset();
                }
                if ( m_CursorLocked ) // hand the cursor back when play ends
                {
                    Input::Mouse::Get().SetCursorMode( Input::MouseState::Visible );
                    m_CursorLocked = false;
                }
                m_LookSuspended = false; // next Play starts captured again
                return;
            }

            auto bodies = registry.view<TransformComponent, RigidBodyComponent, ColliderComponent>();

            if ( !m_World )
            {
                m_World = std::make_unique<Physics::PhysicsWorld>();
                m_World->Init();
            }

            // Create a Jolt body for any physics entity that doesn't have one yet (uses its authored pose).
            for ( auto entity : bodies )
            {
                auto& rb = bodies.get<RigidBodyComponent>( entity );
                if ( rb.RuntimeBody != Physics::kInvalidBody )
                    continue;

                const auto& transform = bodies.get<TransformComponent>( entity );
                const auto& collider  = bodies.get<ColliderComponent>( entity );

                Physics::BodyDesc desc;
                desc.Shape       = collider.Data.Shape;
                desc.HalfExtents = collider.Data.HalfExtents;
                desc.Radius      = collider.Data.Radius;
                desc.HalfHeight  = collider.Data.HalfHeight;
                desc.Type        = rb.Data.Type;
                desc.Mass        = rb.Data.Mass;
                desc.Friction    = rb.Data.Friction;
                desc.Restitution = rb.Data.Restitution;
                desc.Position    = transform.Translation;
                desc.Rotation    = glm::quat( transform.Rotation );

                rb.RuntimeBody = m_World->CreateBody( desc );
            }

            // Create a Jolt CharacterVirtual for any character entity that doesn't have one (authored pose).
            auto characters = registry.view<TransformComponent, CharacterControllerComponent>();
            for ( auto entity : characters )
            {
                auto& cc = characters.get<CharacterControllerComponent>( entity );
                if ( cc.RuntimeCharacter != Physics::kInvalidCharacter )
                    continue;
                const auto& transform = characters.get<TransformComponent>( entity );

                Physics::CharacterDesc desc;
                desc.Radius      = cc.Data.Radius;
                desc.HalfHeight  = glm::max( ( cc.Data.Height - 2.0f * cc.Data.Radius ) * 0.5f, 0.01f );
                desc.Position    = transform.Translation; // capsule center
                desc.MaxSlopeDeg = cc.Data.MaxSlopeDeg;
                cc.RuntimeCharacter   = m_World->CreateCharacter( desc );
                cc.VerticalVelocity   = 0.0f;
            }

            if ( !playing )
                return; // Paused: bodies exist but time is frozen.

            m_World->Step( ts.GetSeconds() );

            // Write the simulated pose back into the transform for moving bodies.
            for ( auto entity : bodies )
            {
                auto& rb = bodies.get<RigidBodyComponent>( entity );
                if ( rb.RuntimeBody == Physics::kInvalidBody || rb.Data.Type == Physics::BodyType::Static )
                    continue;

                auto& transform       = bodies.get<TransformComponent>( entity );
                transform.Translation = m_World->GetPosition( rb.RuntimeBody );
                transform.Rotation    = glm::eulerAngles( m_World->GetRotation( rb.RuntimeBody ) );
            }

            // ---- Character controllers: WASD (camera-relative) + gravity + jump + mouse-look ----
            const float dt = ts.GetSeconds();

            // Mouse-look. Config comes from the (first) character's CharacterControllerData (see the "Camera"
            // category in Details). Two modes:
            //   - Always-on (default): the cursor is CAPTURED on Play and the mouse always steers; press Left
            //     Alt to TOGGLE the cursor free (so you can click Stop / the UI), press again to recapture.
            //   - Hold-RMB: look only while the right mouse button is down; cursor stays free otherwise.
            // Horizontal turns the CHARACTER (yaw — body, child camera and the camera-relative move basis all
            // follow); vertical tilts the child camera only (pitch, clamped). m_LastMouse is refreshed every
            // frame (like EditorCamera) so re-engaging look never produces a one-frame jump.
            ECS::CharacterControllerData cfg{}; // defaults if there's no character this frame
            if ( characters.begin() != characters.end() )
                cfg = characters.get<CharacterControllerComponent>( *characters.begin() ).Data;

            // Left Alt TOGGLES the cursor (edge-detected so a held key doesn't flip every frame).
            const bool altDown = Input::Keyboard::IsKeyPressed( Common::KeyCode::LeftAlt );
            if ( altDown && !m_AltPrev )
                m_LookSuspended = !m_LookSuspended;
            m_AltPrev = altDown;

            const bool rmbHeld     = Input::Mouse::Get().IsMouseButtonPressed( Common::MouseButton::Right );
            const bool lookEnabled = cfg.HoldRMBToLook ? rmbHeld : !m_LookSuspended;

            // Capture the cursor while looking (smooth, unbounded motion); release it otherwise.
            const bool wantLock = lookEnabled;
            bool       cursorJustToggled = false;
            if ( wantLock != m_CursorLocked )
            {
                Input::Mouse::Get().SetCursorMode( wantLock ? Input::MouseState::Locked
                                                            : Input::MouseState::Visible );
                m_CursorLocked    = wantLock;
                cursorJustToggled = true; // GLFW recenters on lock → skip this frame's delta to avoid a jump
            }

            const auto      mp = Input::Mouse::Get().GetMousePosition();
            const glm::vec2 mouseNow( mp.first, mp.second );
            const glm::vec2 mouseDelta =
                 ( lookEnabled && !cursorJustToggled ) ? ( mouseNow - m_LastMouse ) : glm::vec2( 0.0f );
            m_LastMouse = mouseNow;

            const float lookSens   = 0.0035f * cfg.MouseSensitivity; // base radians/pixel * config multiplier
            const float yawDelta   = -mouseDelta.x * lookSens;
            const float pitchDelta = ( cfg.InvertY ? 1.0f : -1.0f ) * mouseDelta.y * lookSens;
            const float pitchLimit = glm::radians( cfg.PitchLimitDeg );

            // Movement basis from the active camera (flattened to the ground plane), so "forward" follows
            // wherever the camera looks. The follow camera itself is a separate child entity.
            glm::vec3 camFwd( 0.0f, 0.0f, -1.0f );
            glm::vec3 camRight( 1.0f, 0.0f, 0.0f );
            if ( auto cam = m_Scene->GetMainCamera().lock() )
            {
                const glm::mat4 c2w = glm::inverse( cam->GetViewMatrix() );
                camFwd              = -glm::vec3( c2w[2] );
                camRight            = glm::vec3( c2w[0] );
            }
            camFwd.y = 0.0f;
            camRight.y = 0.0f;
            if ( glm::length( camFwd ) > 1e-4f )
                camFwd = glm::normalize( camFwd );
            if ( glm::length( camRight ) > 1e-4f )
                camRight = glm::normalize( camRight );

            glm::vec3 wish( 0.0f );
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::W ) ) wish += camFwd;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::S ) ) wish -= camFwd;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::D ) ) wish += camRight;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::A ) ) wish -= camRight;
            if ( glm::length( wish ) > 1e-4f )
                wish = glm::normalize( wish );
            const bool jumpHeld = Input::Keyboard::IsKeyPressed( Common::KeyCode::Space );
            const bool sprint   = Input::Keyboard::IsKeyPressed( Common::KeyCode::LeftShift );

            for ( auto entity : characters )
            {
                auto& cc = characters.get<CharacterControllerComponent>( entity );
                if ( cc.RuntimeCharacter == Physics::kInvalidCharacter )
                    continue;

                const bool onGround = m_World->IsCharacterOnGround( cc.RuntimeCharacter );
                if ( onGround )
                    cc.VerticalVelocity = jumpHeld ? cc.Data.JumpSpeed : -1.0f; // small stick-to-ground
                else
                    cc.VerticalVelocity -= 9.81f * dt;

                const float     moveSpeed = cc.Data.MoveSpeed * ( sprint ? 1.8f : 1.0f ); // LShift = sprint
                const glm::vec3 horiz     = wish * moveSpeed;
                const glm::vec3 vel( horiz.x, cc.VerticalVelocity, horiz.z );
                m_World->UpdateCharacter( cc.RuntimeCharacter, vel, dt );

                cc.CurrentSpeed = glm::length( glm::vec2( horiz.x, horiz.z ) ); // 0 when idle, drives anim

                auto& transform       = characters.get<TransformComponent>( entity );
                transform.Translation = m_World->GetCharacterPosition( cc.RuntimeCharacter );

                // Yaw turns the character itself (CharacterVirtual doesn't rotate the capsule, so we own the
                // yaw); the child camera inherits it through the hierarchy.
                if ( yawDelta != 0.0f )
                    transform.Rotation.y += yawDelta;

                // Pitch lives on the child camera (look up/down without tilting the body). Find the direct
                // child carrying a CameraComponent and clamp its X rotation to avoid gimbal flip.
                if ( pitchDelta != 0.0f && registry.has<RelationshipComponent>( entity ) )
                {
                    for ( entt::entity child : registry.get<RelationshipComponent>( entity ).Children )
                    {
                        if ( !registry.has<CameraComponent>( child ) || !registry.has<TransformComponent>( child ) )
                            continue;
                        auto& camRot = registry.get<TransformComponent>( child ).Rotation;
                        camRot.x     = glm::clamp( camRot.x + pitchDelta, -pitchLimit, pitchLimit );
                        break;
                    }
                }

                // Pick idle/walk/run (or jump while airborne) on the skinned child.
                DriveLocomotion( registry, entity, cc.CurrentSpeed, cc.Data.MoveSpeed, onGround );
            }
        }

        // Selects + plays the procedural locomotion clip on a character's skinned child (the one carrying the
        // humanoid rig) based on its planar speed. The Animator is created/advanced by AnimationECSSystem; we
        // only set which clip plays (and only on change, so it doesn't restart every frame). No-op for a
        // non-humanoid rig (clip bone-tracks are indexed for that specific skeleton).
        static void DriveLocomotion( entt::registry& registry, entt::entity character, float speed,
                                     float baseMoveSpeed, bool onGround )
        {
            if ( !registry.has<RelationshipComponent>( character ) )
                return;

            for ( entt::entity child : registry.get<RelationshipComponent>( character ).Children )
            {
                if ( !registry.has<SkinnedMeshComponent>( child ) || !registry.has<AnimationComponent>( child ) )
                    continue;

                auto& anim = registry.get<AnimationComponent>( child );
                if ( !anim.Animator )
                    return; // created by AnimationECSSystem next frame

                auto* base = Runtime::ResourceRegistry::GetMeshService()->Get(
                     registry.get<SkinnedMeshComponent>( child ).MeshHandle );
                if ( !base || !base->IsSkinned() )
                    return;
                if ( static_cast<SkinnedMesh*>( base )->GetSkeleton().GetSignature() !=
                     Geometry::ProceduralCharacterFactory::GetHumanoidSkeletonSignature() )
                    return; // a different rig — our procedural clips wouldn't map onto it

                const Animation::AnimationClip* clip = nullptr;
                const char*                     name = nullptr;
                if ( !onGround )
                {
                    clip = &Animation::ProceduralCharacterAnimations::Jump();
                    name = "Jump";
                }
                else if ( speed < 0.2f )
                {
                    clip = &Animation::ProceduralCharacterAnimations::Idle();
                    name = "Idle";
                }
                else if ( speed <= baseMoveSpeed * 1.2f )
                {
                    clip = &Animation::ProceduralCharacterAnimations::Walk();
                    name = "Walk";
                }
                else
                {
                    clip = &Animation::ProceduralCharacterAnimations::Run();
                    name = "Run";
                }

                if ( anim.CurrentClip != name )
                {
                    // CrossFade (not Play) so idle<->walk<->run transitions blend smoothly instead of popping.
                    anim.Animator->CrossFade( *clip, 0.18f, true );
                    anim.CurrentClip = name;
                    anim.Playing     = true;
                    anim.Loop        = true;
                }
                return;
            }
        }

    private:
        Core::Scene*                           m_Scene = nullptr;
        std::unique_ptr<Physics::PhysicsWorld> m_World;
        glm::vec2                              m_LastMouse{ 0.0f, 0.0f }; // for mouse-look frame delta
        bool                                   m_CursorLocked  = false;   // is the OS cursor captured for look
        bool                                   m_LookSuspended = false;   // toggled by Left Alt (cursor freed)
        bool                                   m_AltPrev       = false;   // edge-detect the Left Alt toggle
    };
} // namespace Desert::ECS
