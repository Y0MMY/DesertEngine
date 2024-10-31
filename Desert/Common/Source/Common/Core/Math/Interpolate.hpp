#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#define GLM_ENABLE_EXPERIMENTAL

namespace Common::Math
{
    glm::mat4 InterpolateTranslation( const glm::vec3& start, const glm::vec3& end, float timeScale );
    glm::mat4 InterpolateRotation( const glm::quat& start, const glm::quat& end, float timeScale );
    glm::mat4 InterpolateScale( const glm::vec3& start, const glm::vec3& end, float timeScale );
} // namespace Common::Math