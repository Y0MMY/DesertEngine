#include "SpaceTransformer.hpp"

namespace Common::Math
{

    glm::vec2 SpaceTransformer::WorldToScreenSpace( const glm::vec3& worldPos, const glm::mat4& mvp,
                                                    const float width, const float height )
    {
        glm::vec4 trans = mvp * glm::vec4( worldPos, 1.0f );
        trans *= 0.5f / trans.w;
        trans += glm::vec4( 0.5f, 0.5f, 0.0f, 0.0f );
        trans.y = 1.f - trans.y;
        trans.x *= width;
        trans.y *= height;
        return glm::vec2( trans.x, trans.y );
    }

} // namespace Common::Math