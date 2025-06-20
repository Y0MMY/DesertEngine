#pragma once

#include <Common/Core/UUID.hpp>

#include <glm/glm.hpp>

#include <Engine/Graphic/Mesh.hpp>

// #include <rflcpp/rfl.hpp>

namespace Desert::ECS
{
    struct TagComponent
    {
        std::string Tag;
    };

    struct UUIDComponent
    {
        Common::UUID UUID;
    };

    struct StaticMeshComponent
    {
        Common::Filepath      Filepath;
        std::shared_ptr<Mesh> Mesh;
    };

    struct TransformComponent
    {
        glm::vec3 Position;
        glm::vec3 Rotation;
        glm::vec3 Scale;
    };

    struct DirectionLightComponent
    {
    };
} // namespace Desert::ECS