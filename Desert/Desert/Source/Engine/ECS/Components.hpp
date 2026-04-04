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
#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/SkinnedMaterialPBR.hpp>

#include <Engine/Animation/Animator.hpp>
#include <Engine/Animation/FSM/AnimationStateMachine.hpp>

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
        std::optional<PrimitiveType>       PrimitiveShape = std::nullopt; // TODO: std::variant

        std::shared_ptr<Graphic::StaticMaterialPBR> Material;
        bool                                        OutlineDraw = false;

        StaticMeshComponent()
        {
            Material = std::make_shared<Graphic::StaticMaterialPBR>( nullptr );
            // TODO: Do not allocate runtime materials in the component constructor.
            //       Material should be lazily created by the render system (runtime-only),
            //       or provided via a material asset / override mechanism.
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

    struct SkinnedMeshComponent
    {
        Assets::AssetHandle                          MeshHandle;
        std::shared_ptr<Graphic::SkinnedMaterialPBR> Material;

        SkinnedMeshComponent()
        {
            Material = std::make_shared<Graphic::SkinnedMaterialPBR>(
                 nullptr ); // TODO: Do not allocate runtime materials in the component constructor.
                            //       Material should be lazily created by the render system (runtime-only),
                            //       or provided via a material asset / override mechanism.
        }
    };

    struct AnimationComponent
    {
        // active Animator (runtime instance)
        std::unique_ptr<Animation::Animator> Animator;
        //std::unique_ptr<Animation::AnimationStateMachine> StateMachine;

        // current name (debug / editor / FSM)
        std::string CurrentClip;

        bool Playing = true;
        bool Loop    = true;

        float PlaybackSpeed = 1.0f;

        // Root motion
        bool EnableRootMotion = false;

        AnimationComponent() = default;

        explicit AnimationComponent( std::unique_ptr<Animation::Animator>&& animator )
             : Animator( std::move( animator ) )
        {
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