#pragma once

#include <utility>

#include "KeyCodes.hpp"
#include "MouseButton.hpp"

namespace Common::Input
{
	enum class MouseState
	{
		Visible, Hidden, Locked
	};

	class Mouse
	{
	public:
		static Mouse& Get();

		bool IsMouseButtonPressed(MouseButton button, const void* window);
		float GetMouseX(const void* window);
		float GetMouseY(const void* window);
		std::pair<float, float> GetMousePosition(const void* window);

		void SetCursorMode(MouseState mode, const void* window);

		const MouseState GetVisibility() const { return m_MouseMode; }
	private:
		MouseState m_MouseMode = MouseState::Visible;
	};

	class Keyboard
	{
	public:
		static bool IsKeyPressed(KeyCode keycode, const void* window);
	};
}