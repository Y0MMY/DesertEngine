#pragma once

#include <Common/Core/UUID.hpp>
#include <Engine/Assets/Common.hpp>
#include <Engine/Geometry/PrimitiveType.hpp>
#include <Engine/Core/Serialize/GLMReflect.hpp>
#include <Engine/Core/Serialize/CustomReflect.hpp>

#include <rflcpp/rfl/Generic.hpp>
#include <rflcpp/rfl/ExtraFields.hpp>

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
        std::optional<std::vector<std::string>>     MaterialPaths;
        std::optional<Geometry::PrimitiveType>      Primitive;
        std::optional<std::vector<VertexSer>>       CustomVertices;
        std::optional<std::vector<uint32_t>>        CustomIndices;
    };

    struct SkinnedMeshComponentSer
    {
        std::optional<std::string>              MeshPath;
        std::vector<AssetHandle>                MaterialSlots;
        std::optional<std::vector<std::string>> MaterialPaths;
    };

    // NOTE: camera/light payloads are no longer mirrored here — they serialize generically through the
    // reflection registry (ComponentRegistry + ReflectionSerializer). The structs below remain only for
    // the asset-bearing components whose handlers map asset handles <-> file paths.

    struct SkyboxComponentSer
    {
        std::optional<std::string> SkyboxPath;
        float                      Intensity = 1.0f;

        // Procedural atmosphere (engine-generated sky).
        bool  Procedural    = false;
        float SunIntensity  = 22.0f;
        float SunDiskRadius = 0.02f;
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

        // Component payloads keyed by ComponentRegistry key (e.g. "StaticMesh", "DirectionLight").
        // ExtraFields spreads these at the entity's top level on write and captures top-level component
        // keys on read — so the on-disk shape matches the original per-component layout (full back/forward
        // compatibility). Reflected blocks are filled by ReflectionSerializer; asset-bearing ones by
        // custom handlers in ComponentRegistry.
        rfl::ExtraFields<rfl::Generic> Components;
    };

    struct PrefabData
    {
        std::string             Name;
        std::vector<EntityData> Entities;
        Common::UUID            Root;
    };
} // namespace Desert::Assets