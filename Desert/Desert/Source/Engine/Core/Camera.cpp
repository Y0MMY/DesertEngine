#include <Engine/Core/Camera.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Common/Core/Input.hpp>

#include <glm/gtc/quaternion.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Desert::Core
{

    Camera::Camera()
    {
        // REGISTER_EVENT( this, OnEvent );

        const auto width  = EngineContext::GetInstance().GetCurrentWindowWidth();
        const auto height = EngineContext::GetInstance().GetCurrentWindowHeight();

        UpdateProjectionMatrix( width, height );

        constexpr glm::vec3 InitialPosition = { 5, 5, 5 };
        m_Distance                          = glm::distance( InitialPosition, m_FocalPoint );

        m_Yaw   = 3.0f * glm::pi<float>() / 4.0f;
        m_Pitch = glm::pi<float>() / 4.0f;

        m_Location                  = m_FocalPoint - GetForwardDirection() * m_Distance + m_LocationDelta;
        const glm::quat orientation = GetOrientation();

        m_Direction  = glm::eulerAngles( orientation ) * ( 180.f / glm::pi<float>() );
        m_ViewMatrix = glm::translate( glm::mat4( 1.0 ), m_Location ) * glm::toMat4( orientation );
        m_ViewMatrix = glm::inverse( m_ViewMatrix );
    }

    Camera::Camera( const glm::mat4& projectionMatrix ) : m_ProjectionMatrix( projectionMatrix )
    {
        // REGISTER_EVENT( this, OnEvent );
    }

    void Camera::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::KeyPressedEvent>( [this]( Common::KeyPressedEvent& e )
                                                      { return this->OnKeyPress( e ); } );

        eventManager.Notify<Common::MouseMovedEvent>( [this]( Common::MouseMovedEvent& e )
                                                      { return this->OnMouseMove( e ); } );
    }

    void Camera::UpdateProjectionMatrix( const uint32_t width, const uint32_t height )
    {
        m_ProjectionMatrix = glm::perspective( glm::radians( m_FOV ), static_cast<float>( width / height ),
                                               m_NearPlane, m_FarPlane );
    }

    bool Camera::OnKeyPress( Common::KeyPressedEvent& e )
    {

        return false;
    }

    bool Camera::OnMouseMove( Common::MouseMovedEvent& e )
    {

        return false;
    }

    void Camera::OnUpdate()
    {
        auto             window = EngineContext::GetInstance().GetCurrentPointerToGLFWwinodw();
        const glm::vec2& MousePosition{ Common::Input::Mouse::Get().GetMouseX( window ),
                                        Common::Input::Mouse::Get().GetMouseY( window ) };
        const glm::vec2  MouseDelta = ( MousePosition - m_InitialMousePosition ) * 0.002f;

        if ( Common::Input::Mouse::Get().IsMouseButtonPressed( Common::MouseButton::Right, window ) )
        {
            const float     YAWSign       = GetUpDirection().y < 0 ? -1.0f : 1.0f;
            constexpr float cameraSpeed   = 0.002f;
            constexpr float rotationSpeed = 0.133f;

            if ( Common::Input::Keyboard::IsKeyPressed( Common::KeyCode::S, window ) )
            {
                m_LocationDelta -= cameraSpeed * m_Direction;
            }
            if ( Common::Input::Keyboard::IsKeyPressed( Common::KeyCode::W, window ) )
            {
                m_LocationDelta += cameraSpeed * m_Direction;
            }
            if ( Common::Input::Keyboard::IsKeyPressed( Common::KeyCode::A, window ) )
            {
                m_LocationDelta -= cameraSpeed * m_RightDirection;
            }
            if ( Common::Input::Keyboard::IsKeyPressed( Common::KeyCode::D, window ) )
            {
                m_LocationDelta += cameraSpeed * m_RightDirection;
            }

            if ( Common::Input::Keyboard::IsKeyPressed( Common::KeyCode::Q, window ) )
            {
                m_LocationDelta -= cameraSpeed * glm::vec3{ 0.f, YAWSign, 0.f };
            }
            if ( Common::Input::Keyboard::IsKeyPressed( Common::KeyCode::E, window ) )
            {
                m_LocationDelta += cameraSpeed * glm::vec3{ 0.f, YAWSign, 0.f };
            }

            constexpr float maxRate = 0.12f;
            m_YawDelta += glm::clamp( YAWSign * MouseDelta.x * rotationSpeed, -maxRate, maxRate );
            m_PitchDelta += glm::clamp( MouseDelta.y * rotationSpeed, -maxRate, maxRate );

            const float distance = glm::distance( m_FocalPoint, m_Location );
            m_FocalPoint         = m_Location + GetForwardDirection() * distance;
            m_Distance           = distance;

            static constexpr float MaxPitch = glm::radians( 89.0f );
            static constexpr float MinPitch = -MaxPitch;
            m_Pitch                         = glm::clamp( m_Pitch, MinPitch, MaxPitch );
        }
        m_InitialMousePosition = MousePosition;
        m_Location += m_LocationDelta;
        m_Yaw += m_YawDelta;
        m_Pitch += m_PitchDelta;
        UpdateCameraView();
    }

    glm::quat Camera::GetOrientation() const
    {
        return glm::quat( glm::vec3( -m_Pitch - m_PitchDelta, -m_Yaw - m_YawDelta, 0.0f ) );
    }

    glm::vec3 Camera::GetUpDirection() const
    {
        return glm::rotate( GetOrientation(), glm::vec3( 0.0, 1.0, 0.0 ) );
    }

    glm::vec3 Camera::GetRightDirection() const
    {
        return glm::rotate( GetOrientation(), glm::vec3( 1.0, 0.0, 0.0 ) );
    }

    glm::vec3 Camera::GetForwardDirection() const
    {
        return glm::rotate( GetOrientation(), glm::vec3( 0.0, 0.0, -1.0 ) );
    }

    void Camera::UpdateCameraView()
    {
        const float     YAWsign       = GetUpDirection().y > 0 ? 1 : -1;
        const glm::vec3 lookDirection = m_Location + GetForwardDirection();
        m_Direction                   = glm::normalize( GetForwardDirection() );
        m_Distance                    = glm::distance( lookDirection, m_FocalPoint );
        m_RightDirection              = glm::cross( m_Direction, glm::vec3{ 0.f, YAWsign, 0.f } );

        m_ViewMatrix = glm::lookAt( m_Location, lookDirection, glm::vec3{ 0.f, YAWsign, 0.f } );

        // Damping
        m_YawDelta *= 0.6f;
        m_PitchDelta *= 0.6f;
        m_LocationDelta *= 0.8f;
    }

} // namespace Desert::Core