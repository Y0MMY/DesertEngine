#pragma once

#include <Common/Core/Events/Event.hpp>
#include <Common/Core/Timestep.hpp>

#include <string>

namespace Common
{
	class Layer
	{
	public:
		Layer(const std::string& name = "DebugName");
		virtual ~Layer() {}

		virtual void OnAttach() = 0;
		virtual void OnDetach() = 0;
		virtual void OnUpdate(Timestep ts) = 0;
		virtual void OnImGuiRender() = 0;
		/*virtual void OnEvent(Event& event) = 0; */

		inline const std::string& GetName() { return m_Name; }
	private:
		std::string m_Name;
	};
}