#pragma once

#include <utility>

#include <Common/Core/KeyCodes.hpp>
#include <Common/Core/MouseButton.hpp>

namespace Desert::Input
{
    enum class MouseState
    {
        Visible,
        Hidden,
        Locked
    };

    class Mouse
    {
    public:
        explicit Mouse();

        static Mouse& Get();

        bool                    IsMouseButtonPressed( Common::MouseButton button );
        float                   GetMouseX();
        float                   GetMouseY();
        std::pair<float, float> GetMousePosition();

        void SetCursorMode( MouseState mode );

        const MouseState GetVisibility() const
        {
            return m_MouseMode;
        }

    private:
        MouseState  m_MouseMode = MouseState::Visible;
        const void* m_Window    = nullptr;
    };

    class Keyboard
    {
    public:
        static bool IsKeyPressed( Common::KeyCode keycode );
    };
} // namespace Desert::Input