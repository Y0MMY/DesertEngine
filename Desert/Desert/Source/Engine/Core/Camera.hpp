#pragma once

#include <Common/Core/Events/Event.hpp>
#include <Common/Core/Events/KeyEvents.hpp>
#include <Common/Core/Events/MouseEvents.hpp>
#include <Engine/Core/Application.hpp>

#include "Frustum.hpp"

#include <Common/Core/EventRegistry.hpp>

#include <glm/glm.hpp>

namespace Desert::Core
{
    class Camera : public Common::EventHandler
    {
    public:
        Camera();
        Camera( const glm::mat4& projectionMatrix );

        glm::mat4 GetProjectionMatrix() const
        {
            return m_ProjectionMatrix;
        }

        glm::mat4 GetViewMatrix() const
        {
            return m_ViewMatrix;
        }

        void OnUpdate( const Common::Timestep& timestep );
        void OnEvent( Common::Event& e ) override;

        const auto GetDirection() const
        {
            return m_Direction;
        }
        void UpdateProjectionMatrix( const uint32_t width, const uint32_t height );

        const auto& GetPosition() const
        {
            return m_Position;
        }

        const Frustum& GetFrustum();

    private:
        bool OnKeyPress( Common::KeyPressedEvent& e );
        bool OnMouseMove( Common::MouseMovedEvent& e );

        glm::quat GetOrientation() const;
        glm::vec3 GetUpDirection() const;
        glm::vec3 GetRightDirection() const;
        glm::vec3 GetForwardDirection() const;

        void UpdateCameraView();

    private:
        glm::mat4 m_ProjectionMatrix = glm::mat4( 1.0f );

        glm::vec3 m_Position      = glm::vec3( 0.0f );
        glm::vec3 m_Orientation   = glm::vec3( 0.0f, 0.0f, -1.0f );
        glm::vec3 m_FocalPoint    = glm::vec3( 0.0f );
        glm::vec3 m_LocationDelta = glm::vec3( 0.0f );

        glm::vec2 m_InitialMousePosition = glm::vec2( 0.0f );

        glm::vec3 m_RightDirection = glm::vec3( 1.0, 0.0f, 0.0f );
        glm::vec3 m_Direction;

        glm::mat4 m_ViewMatrix = glm::mat4( 1.0f );

        float m_Distance  = 0.0f;
        float m_FOV       = 45.0f;
        float m_NearPlane = 0.001f;
        float m_FarPlane  = 1000.0f;

        float m_Pitch = 0.0f, m_PitchDelta = 0.0f;
        float m_Yaw = 0.0f, m_YawDelta = 0.0f;

    private:
        Frustum m_Frustum;
    };
} // namespace Desert::Core