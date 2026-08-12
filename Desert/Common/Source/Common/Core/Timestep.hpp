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
                // Scaled, not duration_cast. The cast is to an INTEGER-representation duration, so it truncated a
                // float-seconds frame time to whole milliseconds: 27.2 ms read as 27, and anything under one
                // millisecond read as ZERO. That is why the title bar showed "Frame: 18.000000ms" beside an FPS
                // that means 27 — and why Camera.cpp, which scales movement by this, moved in whole-millisecond
                // steps and stood still entirely on a sub-millisecond frame.
                inline float GetMilliseconds() const
                {
                    return m_Time.count() * 1000.0f;
                }

                operator float() const { return m_Time.count(); }
	private:
		std::chrono::duration<float> m_Time;
	};
}