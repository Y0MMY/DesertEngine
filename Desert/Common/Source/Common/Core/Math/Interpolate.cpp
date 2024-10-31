#include <Common/Core/Math/Interpolate.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Common::Math
{

    glm::mat4 InterpolateTranslation( const glm::vec3& start, const glm::vec3& end, float timeScale )
    {
        glm::vec3 finalPosition = glm::mix( start, end, timeScale );
        return glm::translate( glm::mat4( 1.0f ), finalPosition );
    }

    glm::mat4 InterpolateRotation( const glm::quat& start, const glm::quat& end, float timeScale )
    {
        glm::quat finalRotation = glm::slerp( start, end, timeScale );
        finalRotation           = glm::normalize( finalRotation );
        return glm::toMat4( finalRotation );
    }

    glm::mat4 InterpolateScale( const glm::vec3& start, const glm::vec3& end, float timeScale )
    {
        glm::vec3 finalScale = glm::mix( start, end, timeScale );
        return glm::scale( glm::mat4( 1.0f ), finalScale );
    }

} // namespace Common::Math