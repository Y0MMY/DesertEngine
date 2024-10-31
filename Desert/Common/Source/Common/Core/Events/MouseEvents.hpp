#pragma once

#include <Common/Core/Events/Event.hpp>
#include <Common/Core/MouseButton.hpp>

#include <sstream>

namespace Common {

	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(float x, float y)
			: m_MouseX(x), m_MouseY(y) {}

		inline float GetX() const { return m_MouseX; }
		inline float GetY() const { return m_MouseY; }

		virtual const EventType GetEventType() const { return GetStaticType(); }
		static EventType GetStaticType() { return EventType::MouseMoved; }

	private:
		float m_MouseX, m_MouseY;
	};

	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(float xOffset, float yOffset)
			: m_XOffset(xOffset), m_YOffset(yOffset) {}

		inline float GetXOffset() const { return m_XOffset; }
		inline float GetYOffset() const { return m_YOffset; }

		virtual const EventType GetEventType() const { return GetStaticType(); }
		static EventType GetStaticType() { return EventType::MouseScroll; }
	private:
		float m_XOffset, m_YOffset;
	};

	class MouseButtonEvent : public Event
	{
	public:
		inline MouseButton GetMouseButton() const { return m_Button; }
	protected:
		explicit MouseButtonEvent(MouseButton button)
			: m_Button(button) {}

		MouseButton m_Button;
	};

	class MouseButtonPressedEvent : public MouseButtonEvent
	{
	public:
		explicit MouseButtonPressedEvent(MouseButton button)
			: MouseButtonEvent(button) {}

		virtual const EventType GetEventType() const { return GetStaticType(); }
		static EventType GetStaticType() { return EventType::MousePressed; }

	};

	//class MouseButtonReleasedEvent : public MouseButtonEvent
	//{
	//public:
	//	MouseButtonReleasedEvent(int button)
	//		: MouseButtonEvent(button) {}

	//};

}