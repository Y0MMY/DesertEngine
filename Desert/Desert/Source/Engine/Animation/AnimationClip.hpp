#pragma once

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/compatibility.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace Desert::Animation
{
    // Initialised for the same reason as the serialization mirrors in Assets/Serialization/Animation.hpp:
    // glm leaves its components indeterminate, and these are the values a clip is sampled from.
    struct PositionKeyFrame
    {
        float     Time     = 0.0f;
        glm::vec3 Position = glm::vec3( 0.0f );

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
        float     Time     = 0.0f;
        glm::quat Rotation = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );

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
        float     Time  = 0.0f;
        glm::vec3 Scale = glm::vec3( 1.0f );

        bool operator<( const ScaleKeyFrame& other ) const
        {
            return Time < other.Time;
        }
        bool operator<( float time ) const
        {
            return Time < time;
        }
    };

    // THE BONE NAME IS THE ONLY BINDING KEY. A `uint32_t BoneIndex` used to sit beside it, uninitialised, and
    // Animator::ResolveTrack has never once read it — it builds name -> track and binds by name, because a
    // clip and the character it drives come from different files with different bone orders. The index was a
    // second answer to a question only the name answers, and the Sequencer's "New Clip" left it unset all the
    // way into the .anim file.
    struct BoneTrack
    {
        std::string BoneName;

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

    // Animation notify / event: a named marker at a time (same unit as Duration / key times). Fires once when
    // playback crosses it; the Animator queues crossed notifies and the ECS dispatches them to scripts.
    struct AnimationNotify
    {
        std::string Name;
        float       Time = 0.0f;
    };

    class AnimationClip
    {
    public:
        std::string AnimationName;
        float       Duration       = 0.0f;
        float       TicksPerSecond = 25.0f;
        // 0 = "no rig claimed", and it needed an initialiser: a default-constructed clip read back
        // whatever was on the heap, and this number is what the animation system matches a skeleton on —
        // so an unset one does not fail to match, it matches something arbitrary. Its neighbours all had
        // one; this field was the exception.
        uint64_t SkeletonSignature = 0;

        // Named tracks, in the order the source file listed them. THIS IS NOT INDEXED BY BONE: it used to be
        // scattered by a serialised bone index, which left unnamed holes wherever the source rig was sparse
        // and made the vector's length a property of the exporter. Playback resolves by name
        // (Animator::ResolveTrack), so position here means nothing and is not allowed to pretend otherwise.
        std::vector<BoneTrack> Tracks;

        std::vector<AnimationNotify> Notifies; // sorted-by-time markers fired during playback
    };
} // namespace Desert::Animation
