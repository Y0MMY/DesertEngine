#pragma once

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/compatibility.hpp>

namespace Desert::Animation
{
    struct PositionKeyFrame
    {
        float     Time;
        glm::vec3 Position;

        bool operator<( const PositionKeyFrame& other ) const
        {
            return Time < other.Time;
        }
        bool operator<( float time ) const
        {
            return Time < time;
        }
    };

    struct RotationKeyFrame
    {
        float     Time;
        glm::quat Rotation;

        bool operator<( const RotationKeyFrame& other ) const
        {
            return Time < other.Time;
        }
        bool operator<( float time ) const
        {
            return Time < time;
        }
    };

    struct ScaleKeyFrame
    {
        float     Time;
        glm::vec3 Scale;

        bool operator<( const ScaleKeyFrame& other ) const
        {
            return Time < other.Time;
        }
        bool operator<( float time ) const
        {
            return Time < time;
        }
    };

    struct BoneTrack
    {
        std::vector<PositionKeyFrame> PositionKeys;
        std::vector<RotationKeyFrame> RotationKeys;
        std::vector<ScaleKeyFrame>    ScaleKeys;

        [[nodiscard]] glm::mat4 GetTransform( float animationTime ) const
        {
            glm::vec3 position = GetInterpolatedPosition( animationTime );
            glm::quat rotation = GetInterpolatedRotation( animationTime );
            glm::vec3 scale    = GetInterpolatedScale( animationTime );

            return glm::translate( glm::mat4( 1.0f ), position ) * glm::toMat4( rotation ) *
                   glm::scale( glm::mat4( 1.0f ), scale );
        }

        [[nodiscard]] glm::vec3 GetInterpolatedPosition( float animationTime ) const
        {
            if ( PositionKeys.empty() )
                return glm::vec3( 0.0f );

            if ( PositionKeys.size() == 1 )
                return PositionKeys[0].Position;

            auto it = std::lower_bound( PositionKeys.begin(), PositionKeys.end(), animationTime );

            if ( it == PositionKeys.begin() )
                return PositionKeys.front().Position;
            if ( it == PositionKeys.end() )
                return PositionKeys.back().Position;

            auto prev = it - 1;
            auto next = it;

            float deltaTime = next->Time - prev->Time;
            float factor    = ( animationTime - prev->Time ) / deltaTime;

            return glm::lerp( prev->Position, next->Position, factor );
        }

        [[nodiscard]] glm::quat GetInterpolatedRotation( float animationTime ) const
        {
            if ( RotationKeys.empty() )
                return glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );

            if ( RotationKeys.size() == 1 )
                return RotationKeys[0].Rotation;

            auto it = std::lower_bound( RotationKeys.begin(), RotationKeys.end(), animationTime );

            if ( it == RotationKeys.begin() )
                return RotationKeys.front().Rotation;
            if ( it == RotationKeys.end() )
                return RotationKeys.back().Rotation;

            auto prev = it - 1;
            auto next = it;

            float deltaTime = next->Time - prev->Time;
            float factor    = ( animationTime - prev->Time ) / deltaTime;

            return glm::slerp( prev->Rotation, next->Rotation, factor );
        }

        [[nodiscard]] glm::vec3 GetInterpolatedScale( float animationTime ) const
        {
            if ( ScaleKeys.empty() )
                return glm::vec3( 1.0f );

            if ( ScaleKeys.size() == 1 )
                return ScaleKeys[0].Scale;

            auto it = std::lower_bound( ScaleKeys.begin(), ScaleKeys.end(), animationTime );

            if ( it == ScaleKeys.begin() )
                return ScaleKeys.front().Scale;
            if ( it == ScaleKeys.end() )
                return ScaleKeys.back().Scale;

            auto prev = it - 1;
            auto next = it;

            float deltaTime = next->Time - prev->Time;
            float factor    = ( animationTime - prev->Time ) / deltaTime;

            return glm::lerp( prev->Scale, next->Scale, factor );
        }
    };

    class AnimationClip
    {
    public:
        float Duration       = 0.0f;
        float TicksPerSecond = 25.0f;

        std::unordered_map<std::string, BoneTrack> Tracks;
    };
} // namespace Desert::Animation
