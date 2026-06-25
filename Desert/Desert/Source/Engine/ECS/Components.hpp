#pragma once

#include <entt/entt.hpp>

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

#include <Engine/Reflection/ReflectionMacros.hpp>

namespace Desert
{
    class Mesh;
    class DynamicMesh;
}

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

    // "Reflected render-data block": editable, reflected fields the editor draws and the renderer maps
    // to its GPU representation. This is the general concept — a surface Material (PBRMaterialData) is
    // just ONE specialization; camera and lights are others. NOT a material, hence the member is `Data`.
    struct CameraData
    {
        REFLECT()

        PROPERTY( DisplayName( "Main Camera" ), Category( "Camera" ) )
        bool IsMainCamera = true;
    };

    struct CameraComponent
    {
        std::shared_ptr<Core::Camera> Camera;
        CameraData                    Data;
    };

    struct VisibilityComponent
    {
        bool Visible;
    };

    struct StaticMeshComponent
    {
        Assets::AssetHandle                       MeshHandle;
        std::vector<Assets::AssetHandle>          MaterialSlots;
        std::vector<Graphic::MaterialInstancePtr> RuntimeMaterialInstances; // Cache to keep instances alive and avoid per-frame allocations
        std::optional<Geometry::PrimitiveType>    Primitive;                // Optional primitive type for dynamic generation
        std::shared_ptr<DynamicMesh>              RuntimeMesh;              // Unique mesh instance for modifications
        bool                                      OutlineDraw = false;
    };

    struct SkinnedMeshComponent
    {
        Assets::AssetHandle                       MeshHandle;
        std::vector<Assets::AssetHandle>          MaterialSlots;
        std::vector<Graphic::MaterialInstancePtr> RuntimeMaterialInstances; // Cache to keep instances alive and avoid per-frame allocations
    };

    struct AnimationComponent
    {
        // active Animator (runtime instance)
        std::unique_ptr<Animation::Animator> Animator;
        // std::unique_ptr<Animation::AnimationStateMachine> StateMachine;

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

    struct DirectionalLightData
    {
        REFLECT()

        PROPERTY( DisplayName( "Color" ), Category( "Light" ), Color )
        glm::vec3 Color = glm::vec3( 1.0f );

        PROPERTY( DisplayName( "Intensity" ), Category( "Light" ), Range( 0.0f, 10.0f ) )
        float Intensity = 1.0f;
    };

    struct DirectionLightComponent
    {
        DirectionalLightData Data;
    };

    // Distance falloff model for a point light. Reflected enum — drives a combo in the editor and
    // round-trips through reflected serialization. (Consumed by the lighting upload path later.)
    enum class LightFalloff
    {
        Linear,
        Quadratic,
        InverseSquare
    };

    struct PointLightData
    {
        REFLECT()

        PROPERTY( DisplayName( "Color" ), Category( "Light" ), Color )
        glm::vec3 Color = glm::vec3( 1.0f );

        PROPERTY( DisplayName( "Intensity" ), Category( "Light" ), Range( 0.0f, 10.0f ) )
        float Intensity = 1.0f;

        PROPERTY( DisplayName( "Radius" ), Category( "Light" ), Range( 0.0f, 100.0f ) )
        float Radius = 10.0f;

        PROPERTY( DisplayName( "Falloff" ), Category( "Light" ) )
        LightFalloff Falloff = LightFalloff::Quadratic;

        PROPERTY( DisplayName( "Show Radius" ), Category( "Light" ) )
        bool ShowRadius = false;
    };

    struct PointLightComponent
    {
        PointLightData Data;
    };

    struct SkyboxComponent
    {
        Assets::AssetHandle SkyboxHandle;

        float Intensity;

        // Engine-generated procedural atmosphere (Rayleigh+Mie). When true, the visible sky is generated
        // in-shader (no HDR asset needed) with the sun driven by the scene's directional light.
        bool  Procedural    = false;
        float SunIntensity  = 22.0f;  // atmosphere sun radiance scale
        float SunDiskRadius = 0.02f;  // sun angular radius (radians)
    };

    struct PrefabComponent
    {
        Assets::AssetHandle Prefab;
    };

    struct RelationshipComponent
    {
        entt::entity              Parent = entt::null;
        std::vector<entt::entity> Children;
    };
} // namespace Desert::ECS