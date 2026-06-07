#pragma once

#include <Common/Core/UUID.hpp>
#include <Engine/Assets/Common.hpp>
#include <Engine/Geometry/PrimitiveType.hpp>
#include <Engine/Core/Serialize/GLMReflect.hpp>
#include <Engine/Core/Serialize/CustomReflect.hpp>

#include <optional>
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace Desert::Assets
{
    struct VertexSer
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;
    };

    struct StaticMeshComponentSer
    {
        std::optional<std::string>                  MeshPath;
        std::vector<AssetHandle>                    MaterialSlots;
        std::optional<Geometry::PrimitiveType>      Primitive;
        std::optional<std::vector<VertexSer>>       CustomVertices;
        std::optional<std::vector<uint32_t>>        CustomIndices;
    };

    struct SkinnedMeshComponentSer
    {
        std::optional<std::string> MeshPath;
        std::vector<AssetHandle>   MaterialSlots;
    };

    struct CameraComponentSer
    {
        bool IsMainCamera = false;
    };

    struct DirectionLightComponentSer
    {
        float Intensity = 1.0f;
    };

    struct PointLightComponentSer
    {
        glm::vec3 Color     = glm::vec3( 1.0f );
        float     Intensity = 1.0f;
        float     Radius    = 10.0f;
    };

    struct SkyboxComponentSer
    {
        std::optional<std::string> SkyboxPath;
        float                      Intensity = 1.0f;
    };

    struct EntityData
    {
        std::optional<Common::UUID> id;
        std::optional<Common::UUID> parent;

        std::optional<std::string> PrefabPath;

        std::optional<std::string> Tag;
        
        // Transform
        std::optional<glm::vec3>   Translation;
        std::optional<glm::vec3>   Rotation;
        std::optional<glm::vec3>   Scale;

        // Components
        std::optional<StaticMeshComponentSer>  StaticMesh;
        std::optional<SkinnedMeshComponentSer> SkinnedMesh;
        std::optional<CameraComponentSer>      Camera;
        std::optional<DirectionLightComponentSer> DirectionLight;
        std::optional<PointLightComponentSer>     PointLight;
        std::optional<SkyboxComponentSer>         Skybox;
    };

    struct PrefabData
    {
        std::string             Name;
        std::vector<EntityData> Entities;
        Common::UUID            Root;
    };
} // namespace Desert::Assets