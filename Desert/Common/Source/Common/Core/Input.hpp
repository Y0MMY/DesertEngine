#pragma once

#include <utility>

#include "KeyCodes.hpp"
#include "MouseButton.hpp"

namespace Common::Input
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

        bool                    IsMouseButtonPressed( MouseButton button );
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
        static bool IsKeyPressed( KeyCode keycode );
    };
} // namespace Common::Input