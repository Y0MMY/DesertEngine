#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/compatibility.hpp>

namespace Desert::Assets::Serialization
{
    struct KeyPosition
    {
        float     Time;
        glm::vec3 Value;
    };

    struct KeyRotation
    {
        float     Time;
        glm::quat Value;
    };

    struct KeyScale
    {
        float     Time;
        glm::vec3 Value;
    };

    struct ChannelData
    {
        std::string BoneName;
        uint32_t    BoneIndex;

        std::vector<KeyPosition> Positions;
        std::vector<KeyRotation> Rotations;
        std::vector<KeyScale>    Scales;
    };

    struct AnimationAssetData
    {
        std::string              Name;
        float                    Duration;
        float                    TicksPerSecond;
        uint64_t                 SkeletonSignature;
        std::vector<ChannelData> Channels;
    };
} // namespace Desert::Assets::Serialization