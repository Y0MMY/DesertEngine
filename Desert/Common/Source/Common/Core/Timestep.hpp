#pragma once

#include <chrono>

namespace Common
{
	class Timestep
	{
	public:
		Timestep();
		explicit Timestep(float seconds);

		inline float GetSeconds() const { return m_Time.count(); }
		inline float GetMilliseconds() const { return std::chrono::duration_cast<std::chrono::milliseconds>(m_Time).count(); }

		operator float() const { return m_Time.count(); }
	private:
		std::chrono::duration<float> m_Time;
	};
}