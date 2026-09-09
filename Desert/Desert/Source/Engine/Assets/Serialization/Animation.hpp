#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/compatibility.hpp>

namespace Desert::Assets::Serialization
{
    // EVERY SCALAR IN THIS FILE CARRIES AN INITIALISER. These structs are what reflect-cpp writes into a
    // `.anim`, so a field left indeterminate is not a runtime accident that the next assignment repairs — it
    // is bytes on disk that outlive the process. glm's vector/quaternion default constructors leave their
    // components indeterminate too, which is why the Value members are spelled out as well.
    struct KeyPosition
    {
        float     Time  = 0.0f;
        glm::vec3 Value = glm::vec3( 0.0f );
    };

    struct KeyRotation
    {
        float     Time  = 0.0f;
        glm::quat Value = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
    };

    struct KeyScale
    {
        float     Time  = 0.0f;
        glm::vec3 Value = glm::vec3( 1.0f );
    };

    // NO BONE INDEX. It was here, uninitialised, and the Sequencer wrote whatever the stack held into every
    // clip it saved; the loader then sized the track array from it. The bone NAME is what playback binds on,
    // it is what an animation and a character actually share, and an empty name is a refusal the loader can
    // see — which is the whole property an index of 0 could never have.
    struct ChannelData
    {
        std::string BoneName;

        std::vector<KeyPosition> Positions;
        std::vector<KeyRotation> Rotations;
        std::vector<KeyScale>    Scales;
    };

    // Animation notify / event: a named marker at a time (same unit as Duration / key times) that fires once
    // when playback crosses it — dispatched to the entity's scripts as OnAnimationNotify(name). Footstep,
    // hit-frame, "spawn VFX", etc.
    struct NotifyData
    {
        std::string Name;
        float       Time = 0.0f;
    };

    struct AnimationAssetData
    {
        std::string              Name;
        float                    Duration          = 0.0f;
        float                    TicksPerSecond    = 25.0f; // the fallback Animator::UpdatePlayback applies
        uint64_t                 SkeletonSignature = 0;     // 0 = "no rig claimed", as on AnimationClip
        std::vector<ChannelData> Channels;
        // New field — clips cooked before notifies existed load with rfl::DefaultIfMissing (empty list).
        std::vector<NotifyData>  Notifies;
    };
} // namespace Desert::Assets::Serialization