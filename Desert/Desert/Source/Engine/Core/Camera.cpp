#include <Engine/Core/Camera.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Core/Input.hpp>

#include <glm/gtc/quaternion.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Desert::Core
{
    // ─── Camera (base) ──────────────────────────────────────────────────────────
    void Camera::UpdateProjectionMatrix( const uint32_t width, const uint32_t height )
    {
        const float aspectRatio = static_cast<float>( width ) / static_cast<float>( height == 0 ? 1 : height );
        m_ProjectionMatrix      = glm::perspective( glm::radians( m_FOV ), aspectRatio, m_NearPlane, m_FarPlane );
    }

    const Frustum& Camera::GetFrustum()
    {
        m_Frustum.Rebuild( m_ProjectionMatrix, m_ViewMatrix );
        return m_Frustum;
    }

    // ─── EditorCamera (orbit / fly) ─────────────────────────────────────────────
    EditorCamera::EditorCamera()
    {
        const auto window = EngineContext::GetInstance().GetWindow();
        const auto width  = window ? window->GetWidth() : 1280;
        const auto height = window ? window->GetHeight() : 720;

        UpdateProjectionMatrix( width, height );

        constexpr glm::vec3 InitialPosition = { 5, 5, 5 };
        m_Distance                          = glm::distance( InitialPosition, m_FocalPoint );

        m_Yaw   = 3.0f * glm::pi<float>() / 4.0f;
        m_Pitch = glm::pi<float>() / 4.0f;

        m_Position                  = m_FocalPoint - GetForwardDirection() * m_Distance + m_LocationDelta;
        const glm::quat orientation = GetOrientation();

        m_Direction  = glm::eulerAngles( orientation ) * ( 180.f / glm::pi<float>() );
        m_ViewMatrix = glm::translate( glm::mat4( 1.0 ), m_Position ) * glm::toMat4( orientation );
        m_ViewMatrix = glm::inverse( m_ViewMatrix );
    }

    EditorCamera::EditorCamera( const glm::mat4& projectionMatrix )
    {
        m_ProjectionMatrix = projectionMatrix;
        m_Direction        = glm::vec3( 90.0f, 0.0f, 0.0f );
        m_FocalPoint       = glm::vec3( 0.0f );

        const glm::vec3 position = { -5, 5, 5 };
        m_Distance              = glm::distance( position, m_FocalPoint );

        m_Yaw   = 3.0f * glm::pi<float>() / 4.0f;
        m_Pitch = glm::pi<float>() / 4.0f;

        m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
        UpdateCameraView();
    }

    void EditorCamera::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::KeyPressedEvent>( [this]( Common::KeyPressedEvent& e )
                                                      { return this->OnKeyPress( e ); } );

        eventManager.Notify<Common::MouseMovedEvent>( [this]( Common::MouseMovedEvent& e )
                                                      { return this->OnMouseMove( e ); } );
    }

    bool EditorCamera::OnKeyPress( Common::KeyPressedEvent& e )
    {
        return false;
    }

    bool EditorCamera::OnMouseMove( Common::MouseMovedEvent& e )
    {
        return false;
    }

    void EditorCamera::OnUpdate( const Common::Timestep& timestep )
    {
        const glm::vec2& MousePosition{ Input::Mouse::Get().GetMouseX(), Input::Mouse::Get().GetMouseY() };
        const glm::vec2  MouseDelta = ( MousePosition - m_InitialMousePosition ) * 0.002f;

        const bool mousePressed = Input::Mouse::Get().IsMouseButtonPressed( Common::MouseButton::Right );

        // An RMB-look only STARTS inside the viewport, but stays active until release so the drag
        // can leave the window. Keyboard movement works whenever the viewport is hovered — no RMB
        // required (UE-style).
        if ( mousePressed && m_InputEnabled )
            m_Flying = true;
        if ( !mousePressed )
            m_Flying = false;

        const bool allowKeyboard = m_InputEnabled || m_Flying;

        if ( allowKeyboard )
        {
            const float YAWSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;

            // Shift = x4 boost, Ctrl = x0.25 precision crawl.
            float speedScale = 1.0f;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::LeftShift ) ||
                 Input::Keyboard::IsKeyPressed( Common::KeyCode::RightShift ) )
                speedScale *= 4.0f;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::LeftControl ) ||
                 Input::Keyboard::IsKeyPressed( Common::KeyCode::RightControl ) )
                speedScale *= 0.25f;

            const float cameraSpeed   = 0.0002f * m_MovementSpeed * speedScale * timestep.GetMilliseconds();
            const float rotationSpeed = 0.133f * timestep.GetMilliseconds();

            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::S ) )
                m_LocationDelta -= cameraSpeed * m_Direction;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::W ) )
                m_LocationDelta += cameraSpeed * m_Direction;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::A ) )
                m_LocationDelta -= cameraSpeed * m_RightDirection;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::D ) )
                m_LocationDelta += cameraSpeed * m_RightDirection;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::Q ) )
                m_LocationDelta -= cameraSpeed * glm::vec3{ 0.f, YAWSign, 0.f };
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::E ) )
                m_LocationDelta += cameraSpeed * glm::vec3{ 0.f, YAWSign, 0.f };

            // Arrow keys rotate without touching the mouse: left/right = yaw, up/down = pitch.
            constexpr float arrowRate = 0.0022f;
            const float     arrowStep = arrowRate * rotationSpeed;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::Left ) )
                m_YawDelta -= arrowStep;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::Right ) )
                m_YawDelta += arrowStep;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::Up ) )
                m_PitchDelta -= arrowStep;
            if ( Input::Keyboard::IsKeyPressed( Common::KeyCode::Down ) )
                m_PitchDelta += arrowStep;
        }

        if ( m_Flying )
        {
            const float     YAWSign       = GetUpDirection().y < 0 ? -1.0f : 1.0f;
            const float     rotationSpeed = 0.133f * timestep.GetMilliseconds();
            constexpr float maxRate       = 0.12f;
            m_YawDelta   += glm::clamp( YAWSign * MouseDelta.x * rotationSpeed, -maxRate, maxRate );
            m_PitchDelta += glm::clamp( MouseDelta.y * rotationSpeed, -maxRate, maxRate );
        }

        m_InitialMousePosition = MousePosition;

        m_Position += m_LocationDelta;
        m_Yaw      += m_YawDelta;
        m_Pitch    += m_PitchDelta;

        // Clamp pitch BEFORE computing the view matrix — exceeding ±90° makes the lookAt target parallel to
        // the up vector, producing a degenerate matrix that collapses all vertices to one clip position.
        static constexpr float kMaxPitch = glm::radians( 89.0f );
        m_Pitch = glm::clamp( m_Pitch, -kMaxPitch, kMaxPitch );

        if ( m_Flying )
        {
            const float distance = glm::distance( m_FocalPoint, m_Position );
            m_FocalPoint         = m_Position + GetForwardDirection() * distance;
            m_Distance           = distance;
        }

        UpdateCameraView();
    }

    glm::quat EditorCamera::GetOrientation() const
    {
        return glm::quat( glm::vec3( -m_Pitch, -m_Yaw, 0.0f ) );
    }

    glm::vec3 EditorCamera::GetUpDirection() const
    {
        return glm::rotate( GetOrientation(), glm::vec3( 0.0, 1.0, 0.0 ) );
    }

    glm::vec3 EditorCamera::GetRightDirection() const
    {
        return glm::rotate( GetOrientation(), glm::vec3( 1.0, 0.0, 0.0 ) );
    }

    glm::vec3 EditorCamera::GetForwardDirection() const
    {
        return glm::rotate( GetOrientation(), glm::vec3( 0.0, 0.0, -1.0 ) );
    }

    void EditorCamera::UpdateCameraView()
    {
        const float     YAWsign       = GetUpDirection().y > 0 ? 1 : -1;
        const glm::vec3 lookDirection = m_Position + GetForwardDirection();
        m_Direction                   = glm::normalize( GetForwardDirection() );
        m_Distance                    = glm::distance( lookDirection, m_FocalPoint );
        m_RightDirection              = glm::cross( m_Direction, glm::vec3{ 0.f, YAWsign, 0.f } );

        m_ViewMatrix = glm::lookAt( m_Position, lookDirection, glm::vec3{ 0.f, YAWsign, 0.f } );

        // Damping
        m_YawDelta *= 0.6f;
        m_PitchDelta *= 0.6f;
        m_LocationDelta *= 0.8f;
    }

    // ─── GameplayCamera (driven by a CameraComponent) ───────────────────────────
    void GameplayCamera::SetFromTransform( const glm::vec3& position, const glm::vec3& eulerRotation,
                                           float fovDegrees, float nearPlane, float farPlane, uint32_t width,
                                           uint32_t height )
    {
        m_Position  = position;
        m_FOV       = fovDegrees;
        m_NearPlane = nearPlane;
        m_FarPlane  = farPlane;

        const glm::quat orientation = glm::quat( eulerRotation );
        const glm::vec3 forward     = glm::rotate( orientation, glm::vec3( 0.0f, 0.0f, -1.0f ) );
        const glm::vec3 up          = glm::rotate( orientation, glm::vec3( 0.0f, 1.0f, 0.0f ) );
        m_ViewMatrix                = glm::lookAt( position, position + forward, up );

        UpdateProjectionMatrix( width, height );
    }
} // namespace Desert::Core
