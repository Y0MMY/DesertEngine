#pragma once

#include <Common/Core/UUID.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Geometry/PrimitiveType.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

#include <Engine/Assets/Common.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/MaterialPBR.hpp>

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

    struct CameraComponent
    {
        std::shared_ptr<Core::Camera> Camera;
        bool                          IsMainCamera = true;
    };

    struct VisibilityComponent
    {
        bool Visible;
    };

    struct StaticMeshComponent
    {
        enum class Type : uint8_t
        {
            None,
            Asset,
            Primitive
        };
        std::optional<Assets::AssetHandle> MeshHandle     = std::nullopt;
        std::optional<PrimitiveType>       PrimitiveShape = std::nullopt;

        std::shared_ptr<Graphic::MaterialPBR> Material;
        bool                                  OutlineDraw = false;

        StaticMeshComponent()
        {
            Material = std::make_shared<Graphic::MaterialPBR>( nullptr );
        }

        Type GetMeshType() const
        {
            if ( MeshHandle.has_value() )
                return Type::Asset;
            if ( PrimitiveShape.has_value() )
                return Type::Primitive;
            return Type::None;
        }
    };

    struct TransformComponent
    {
        glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation    = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale       = { 1.0f, 1.0f, 1.0f };

        glm::mat4 GetTransform() const
        {
            return glm::translate( glm::mat4( 1.0f ), Translation ) * glm::toMat4( glm::quat( Rotation ) ) *
                   glm::scale( glm::mat4( 1.0f ), Scale );
        }
    };

    struct DirectionLightComponent
    {
        float Intensity;
    };

    struct PointLightComponent
    {
        glm::vec3 Color;
        glm::vec3 Position;
        float     Intensity;
        float     Radius;

        bool ShowRadius;
    };

    struct SkyboxComponent
    {
        Assets::AssetHandle SkyboxHandle;

        float Intensity;
    };
} // namespace Desert::ECS