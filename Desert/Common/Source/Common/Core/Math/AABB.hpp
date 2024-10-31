#pragma once

#include <glm/glm.hpp>

namespace Common::Math
{
	struct AABB
	{
		glm::vec3 Min = glm::vec3(0.0f);
		glm::vec3 Max = glm::vec3(0.0f);
	};
}