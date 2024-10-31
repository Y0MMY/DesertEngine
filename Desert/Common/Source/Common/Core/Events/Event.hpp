#pragma once

#include <functional>

namespace Common
{
	enum class EventType
	{
		None = 0,
		WindowClose, WindowResize, // Window
		KeyPressed, // Keys
		MouseMoved, MouseScroll, MousePressed// Mouse
	};

	class Event
	{
	public:
		virtual const EventType GetEventType() const = 0;

		bool m_Handled = false;
	};

	class EventManager final // NOTE: Should be static ? 
	{
	public:
		template <typename T>
		using EventFN = std::function<bool(T&)>;

		explicit EventManager(Event& e)
			: m_Event(e) {}

		template <typename T>
		bool Notify(EventFN<T> func)
		{
			if (m_Event.GetEventType() == T::GetStaticType())
			{
				m_Event.m_Handled = func(*(T*)&m_Event); //TODO: static_cast
				return true;
			}
			return false;
		}

		Event& m_Event;

	};

}