#pragma once

#include <Common/Core/Events/Event.hpp>

#include <cstdint>

namespace Common
{
	class EventWindowClose : public Event
	{
	public:
		virtual const EventType GetEventType() const { return GetStaticType(); }
		static EventType GetStaticType() { return EventType::WindowClose; }
	};

	class EventWindowResize : public Event
	{
	public:
		virtual const EventType GetEventType() const { return GetStaticType(); }
		static EventType GetStaticType() { return EventType::WindowResize; }
		EventWindowResize(uint32_t width, uint32_t height)
			: width(width), height(height)
		{}
		EventWindowResize() = delete;

		uint32_t width; uint32_t height;
	};
}