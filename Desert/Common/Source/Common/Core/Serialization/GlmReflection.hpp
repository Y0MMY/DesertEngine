#pragma once

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/compatibility.hpp>

namespace rfl
{
    template <>
    struct Reflector<glm::mat4>
    {
        using ReflType = std::array<float, 16>;

        static inline glm::mat4 to( const ReflType& arr ) noexcept
        {
            glm::mat4 result( 1.0f );

            // column-major layout
            result[0][0] = arr[0];
            result[0][1] = arr[1];
            result[0][2] = arr[2];
            result[0][3] = arr[3];

            result[1][0] = arr[4];
            result[1][1] = arr[5];
            result[1][2] = arr[6];
            result[1][3] = arr[7];

            result[2][0] = arr[8];
            result[2][1] = arr[9];
            result[2][2] = arr[10];
            result[2][3] = arr[11];

            result[3][0] = arr[12];
            result[3][1] = arr[13];
            result[3][2] = arr[14];
            result[3][3] = arr[15];

            return result;
        }

        static inline ReflType from( const glm::mat4& m )
        {
            return { m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1], m[1][2], m[1][3],
                     m[2][0], m[2][1], m[2][2], m[2][3], m[3][0], m[3][1], m[3][2], m[3][3] };
        }
    };

    template <>
    struct rfl::Reflector<glm::vec2>
    {
        using ReflType = std::array<float, 2>;

        static inline glm::vec2 to( const ReflType& arr ) noexcept
        {
            return glm::vec2( arr[0], arr[1] );
        }

        static inline ReflType from( const glm::vec2& v )
        {
            return { v.x, v.y };
        }
    };

    template <>
    struct Reflector<glm::vec3>
    {
        using ReflType = std::array<float, 3>;

        static inline glm::vec3 to( const ReflType& arr ) noexcept
        {
            return glm::vec3( arr[0], arr[1], arr[2] );
        }

        static inline ReflType from( const glm::vec3& v )
        {
            return { v.x, v.y, v.z };
        }
    };

    template <>
    struct Reflector<glm::quat>
    {
        using ReflType = std::array<float, 4>;

        static inline ReflType from( const glm::quat& v )
        {
            return { v.w, v.x, v.y, v.z };
        }

        static inline glm::quat to( const ReflType& arr ) noexcept
        {
            return glm::quat( arr[0], arr[1], arr[2], arr[3] );
        }
    };
} // namespace rfl