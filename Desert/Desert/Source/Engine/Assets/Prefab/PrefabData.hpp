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
#include <array>
#include <glm/glm.hpp>

namespace Desert::Assets
{
    struct VertexSer
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;
    };

    // Mesh component serialization mirrors. Meshes keep a custom (non-reflected) serializer because they
    // carry DERIVED data reflection can't express: dynamic/edited geometry (CustomVertices/CustomIndices,
    // extracted from the transient RuntimeMesh) and a std::optional primitive type. Asset references
    // (MeshPath / MaterialPaths) round-trip as paths through the shared AssetResolver — same code path the
    // reflected components use.
    // Asset references persist BOTH ways (asset-database):
    //   *Guid  — the stable asset handle (survives file renames/moves; preferred on load)
    //   *Path  — human-readable fallback + back-compat with pre-GUID scenes
    struct StaticMeshComponentSer
    {
        std::optional<std::string>                  MeshPath;
        std::optional<uint64_t>                     MeshGuid;
        std::optional<std::vector<std::string>>     MaterialPaths;
        std::optional<std::vector<uint64_t>>        MaterialGuids;
        std::optional<Geometry::PrimitiveType>      Primitive;
        std::optional<std::vector<VertexSer>>       CustomVertices;
        std::optional<std::vector<uint32_t>>        CustomIndices;
        // Rendering controls (absent = component default, so pre-existing scenes stay loadable).
        std::optional<bool>                         OutlineDraw;
        std::optional<int>                          ForcedLOD;
        std::optional<int>                          LODBias;
        std::optional<bool>                         CastShadows;
        std::optional<bool>                         ReceiveShadows;
        std::optional<uint64_t>                     HiddenSubmeshes;
    };

    struct SkinnedMeshComponentSer
    {
        std::optional<std::string>              MeshPath;
        std::optional<uint64_t>                 MeshGuid;
        std::optional<std::vector<std::string>> MaterialPaths;
        std::optional<std::vector<uint64_t>>    MaterialGuids;
    };

    // UE-style Instanced Static Mesh mirror: one mesh (asset path OR primitive) + N per-instance world
    // matrices, each flattened to 16 floats (column-major) so it serializes natively (no glm::mat4 reflector
    // needed in this translation unit).
    struct InstancedStaticMeshComponentSer
    {
        std::optional<std::string>                       MeshPath;
        std::optional<std::vector<std::string>>          MaterialPaths;
        std::optional<Geometry::PrimitiveType>           Primitive;
        std::optional<std::vector<std::array<float, 16>>> InstanceTransforms;
    };

    // MaterialComponent (generic data-driven material) mirror. Param values reflect directly (glm::vec4 via
    // GLMReflect); texture refs round-trip as cooked paths through the AssetResolver ("TextureAsset").
    struct MaterialParamSer
    {
        std::string Name;
        glm::vec4   Value;
    };

    struct MaterialTextureSer
    {
        std::string             Name;
        std::string             Path;
        std::optional<uint64_t> Guid; // stable texture handle (preferred on load; Path = fallback)
    };

    struct MaterialComponentSer
    {
        std::string                                    ShaderName;
        std::optional<std::vector<MaterialParamSer>>   Params;
        std::optional<std::vector<MaterialTextureSer>> Textures;
    };

    // TextComponent mirror — only the authored fields (the glyph mesh is transient, rebuilt at runtime).
    struct TextComponentSer
    {
        std::string Text;
        std::string FontPath;
        glm::vec4   Color             = glm::vec4( 1.0f );
        float       Size              = 1.0f;
        float       EmissiveIntensity = 1.0f;
        bool        Billboard         = false;
    };

    // AnimationComponent mirror — playback settings + the AnimGraph as JSON (the Animator + graph evaluator are
    // transient runtime objects, rebuilt from the mesh skeleton / this JSON). GraphJson is empty when the
    // entity has no state machine.
    struct AnimationComponentSer
    {
        std::string CurrentClip;
        bool        Playing          = true;
        bool        Loop             = true;
        float       PlaybackSpeed    = 1.0f;
        bool        EnableRootMotion = false;
        std::string GraphJson;
    };

    // NOTE: camera/light/skybox payloads are no longer mirrored here — they serialize generically through
    // the reflection registry (ComponentRegistry + ReflectionSerializer + AssetResolver). Only the mesh
    // mirrors above remain (derived geometry isn't reflectable).

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