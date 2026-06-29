#pragma once

#include <Common/Core/Events/Event.hpp>
#include <Common/Core/Events/KeyEvents.hpp>
#include <Common/Core/Events/MouseEvents.hpp>
#include <Engine/Core/Application.hpp>

#include "Frustum.hpp"

#include <Common/Core/EventRegistry.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Desert::Core
{
    // Base camera: owns the view/projection matrices + frustum. Concrete cameras (EditorCamera, the
    // input-driven viewport camera; GameplayCamera, driven by a scene CameraComponent) fill the matrices.
    // The renderer only ever sees this interface.
    class Camera
    {
    public:
        virtual ~Camera() = default;

        [[nodiscard]] virtual glm::mat4 GetViewMatrix() const { return m_ViewMatrix; }
        [[nodiscard]] virtual glm::mat4 GetProjectionMatrix() const { return m_ProjectionMatrix; }

        [[nodiscard]] const glm::vec3& GetPosition() const { return m_Position; }
        [[nodiscard]] float            GetNear() const { return m_NearPlane; }
        [[nodiscard]] float            GetFar() const { return m_FarPlane; }
        [[nodiscard]] float            GetFOV() const { return m_FOV; }

        virtual void OnUpdate( const Common::Timestep& timestep ) {}

        // Rebuild the projection from the current FOV/near/far for a new viewport aspect.
        virtual void UpdateProjectionMatrix( const uint32_t width, const uint32_t height );

        const Frustum& GetFrustum();

    protected:
        glm::mat4 m_ProjectionMatrix = glm::mat4( 1.0f );
        glm::mat4 m_ViewMatrix       = glm::mat4( 1.0f );
        glm::vec3 m_Position         = glm::vec3( 0.0f );

        float m_FOV       = 45.0f;
        float m_NearPlane = 0.001f;
        float m_FarPlane  = 1000.0f;

        Frustum m_Frustum;
    };

    // Free-orbit / fly viewport camera (RMB to look + WASDQE to move). Receives input globally via
    // EventHandler. This is what the editor renders through in Edit mode.
    class EditorCamera : public Camera, public Common::EventHandler
    {
    public:
        EditorCamera();
        explicit EditorCamera( const glm::mat4& projectionMatrix );

        void OnUpdate( const Common::Timestep& timestep ) override;
        void OnEvent( Common::Event& e ) override;

        [[nodiscard]] const auto& GetDirection() const { return m_Direction; }

        // Editor fly-camera movement speed multiplier (1.0 = default). Exposed in the viewport overlay.
        [[nodiscard]] float GetMovementSpeed() const { return m_MovementSpeed; }
        void                SetMovementSpeed( float speed ) { m_MovementSpeed = speed; }

    private:
        bool OnKeyPress( Common::KeyPressedEvent& e );
        bool OnMouseMove( Common::MouseMovedEvent& e );

        glm::quat GetOrientation() const;
        glm::vec3 GetUpDirection() const;
        glm::vec3 GetRightDirection() const;
        glm::vec3 GetForwardDirection() const;

        void UpdateCameraView();

    private:
        glm::vec3 m_Orientation   = glm::vec3( 0.0f, 0.0f, -1.0f );
        glm::vec3 m_FocalPoint    = glm::vec3( 0.0f );
        glm::vec3 m_LocationDelta = glm::vec3( 0.0f );

        glm::vec2 m_InitialMousePosition = glm::vec2( 0.0f );

        glm::vec3 m_RightDirection = glm::vec3( 1.0, 0.0f, 0.0f );
        glm::vec3 m_Direction;

        float m_Distance = 0.0f;
        float m_Pitch = 0.0f, m_PitchDelta = 0.0f;
        float m_Yaw = 0.0f, m_YawDelta = 0.0f;
        float m_MovementSpeed = 1.0f; // multiplies the base fly speed (user-adjustable)
    };

    // Camera driven by a scene entity (CameraComponent): view from the entity transform, projection from
    // the component's FOV/Near/Far. This is what the editor renders through in Play mode.
    class GameplayCamera : public Camera
    {
    public:
        GameplayCamera() = default;

        // Set from the entity's world position + rotation (Euler radians) and the component params.
        void SetFromTransform( const glm::vec3& position, const glm::vec3& eulerRotation, float fovDegrees,
                               float nearPlane, float farPlane, uint32_t width, uint32_t height );
    };
} // namespace Desert::Core
