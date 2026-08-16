#pragma once

#include <Engine/ECS/CloudVolumeComponent.hpp>

#include <glm/glm.hpp>

#include <vector>

namespace Desert::Graphic
{
    /**
     * One placed hero cloud, as the ECS hands it across: the entity's world matrix and the component's
     * own fields, and nothing else.
     *
     * NOT the GPU record. CloudVolumeInstance needs the baked extent, which lives in the `.dvol` header,
     * and the atlas tile index, which only exists after the renderer has taken a lease — neither of which
     * an ECS system may reach for (assets and GPU resources are the render layer's). So the ECS collects
     * placements and the renderer turns each into an instance. That split is also what keeps
     * VolumetricCloudsECSSystem parallel-capable: it reads components and writes a command, exactly as
     * before.
     */
    struct CloudVolumePlacement
    {
        glm::mat4            WorldTransform{ 1.0f };
        ECS::CloudVolumeData Data{};
    };

    using CloudVolumePlacements = std::vector<CloudVolumePlacement>;
} // namespace Desert::Graphic
