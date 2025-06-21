#pragma once

#include <Common/Core/UUID.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Engine/Graphic/Mesh.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

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
        glm::vec3 Position{ 1.0 };
        glm::vec3 Rotation{ 1.0 };
        glm::vec3 Scale{ 1.0 };

        glm::mat4 GetTransform() const
        {
            return glm::translate( glm::mat4( 1.0f ), Position ) * glm::toMat4( glm::quat( Rotation ) ) *
                   glm::scale( glm::mat4( 1.0f ), Scale );
        }
    };

    struct DirectionLightComponent
    {
    };

    struct SkyboxComponent
    {
        Common::Filepath     Filepath;
        Graphic::Environment Env;
    };
} // namespace Desert::ECS