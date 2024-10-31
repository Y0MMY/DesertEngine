#include <Common/Core/Input.hpp>

#include <GLFW/glfw3.h>

namespace Common::Input
{
	bool Keyboard::IsKeyPressed(KeyCode key, const void* window)
	{
		auto state = glfwGetKey((GLFWwindow*)window, static_cast<int32_t>(key));

		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	bool Mouse::IsMouseButtonPressed(MouseButton button, const void* window )
	{
		auto state = glfwGetMouseButton((GLFWwindow*)window, static_cast<int32_t>(button));

		return state == GLFW_PRESS;
	}

	float Mouse::GetMouseX(const void* window)
	{
		auto [x, y] = GetMousePosition(window);
		return (float)x;
	}

	float Mouse::GetMouseY(const void* window)
	{
		auto [x, y] = GetMousePosition(window);
		return (float)y;
	}

	std::pair<float, float> Mouse::GetMousePosition(const void* window)
	{
		double x, y;
		glfwGetCursorPos((GLFWwindow*)window, &x, &y);
		return { (float)x, (float)y };
	}

	Mouse& Mouse::Get()
	{
		static Mouse s_Instance;
		return s_Instance;
	}

	void Mouse::SetCursorMode(MouseState mode, const void* window)
	{
		m_MouseMode = mode;

		glfwSetInputMode((GLFWwindow*)(window), GLFW_CURSOR, GLFW_CURSOR_NORMAL + (int)mode);
	}

}