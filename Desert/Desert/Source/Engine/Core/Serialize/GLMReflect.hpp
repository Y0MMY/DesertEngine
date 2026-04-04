#pragma once

#include <glm/glm.hpp>
#include <rflcpp/rfl.hpp>

namespace rfl
{
    template <>
    struct Reflector<glm::vec2>
    {
        using ReflType = std::array<float, 2>;

        static glm::vec2 to( const ReflType& v ) noexcept
        {
            return { v[0], v[1] };
        }

        static ReflType from( const glm::vec2& v ) noexcept
        {
            return { v.x, v.y };
        }
    };

    template <>
    struct Reflector<glm::vec3>
    {
        using ReflType = std::array<float, 3>;

        static glm::vec3 to( const ReflType& v ) noexcept
        {
            return { v[0], v[1], v[2] };
        }

        static ReflType from( const glm::vec3& v ) noexcept
        {
            return { v.x, v.y, v.z };
        }
    };

    template <>
    struct Reflector<glm::vec4>
    {
        using ReflType = std::array<float, 4>;

        static glm::vec4 to( const ReflType& v ) noexcept
        {
            return { v[0], v[1], v[2], v[3] };
        }

        static ReflType from( const glm::vec4& v ) noexcept
        {
            return { v.x, v.y, v.z, v.w };
        }
    };
} // namespace rfl
