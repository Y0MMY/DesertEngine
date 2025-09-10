#pragma once

#include <glm/glm.hpp>

namespace Common::Math
{
    class SpaceTransformer final
    {
    public:
        static glm::vec2 WorldToScreenSpace( const glm::vec3& worldPos, const glm::mat4& mvp, const float width,
                                             const float height );
    };
} // namespace Common::Math