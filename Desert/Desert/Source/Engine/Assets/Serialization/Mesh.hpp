#pragma once

#include <vector>
#include <array>
#include <string>
#include <glm/glm.hpp>
#include <Common/Core/Math/AABB.hpp>

#include <Common/Core/UUID.hpp>
#include <Engine/Assets/Common.hpp>

namespace Desert::Assets::Serialization
{
    struct StaticVertexData
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec3 Tangent;
        glm::vec3 Bitangent;
        glm::vec2 TexCoord;
    };

    struct SkinnedVertexData
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec3 Tangent;
        glm::vec3 Bitangent;
        glm::vec2 TexCoord;

        std::array<uint32_t, 4> BoneIDs     = { 0, 0, 0, 0 };
        std::array<float, 4>    BoneWeights = { 0, 0, 0, 0 };
    };

    struct IndexData
    {
        uint32_t V1, V2, V3;
    };

    struct SubmeshData
    {
        std::string        Name;
        uint32_t           VertexOffset;
        uint32_t           VertexCount;
        uint32_t           IndexOffset;
        uint32_t           IndexCount;
        glm::mat4          Transform;
        Common::Math::AABB BoundingBox;

        Common::UUID MaterialHandle;
    };

    struct MeshAssetData
    {
        bool                           IsSkinned = false;
        std::vector<StaticVertexData>  StaticVertices;
        std::vector<SkinnedVertexData> SkinnedVertices;
        std::vector<IndexData>         Indices;
        std::vector<SubmeshData>       Submeshes;
        std::optional<uint64_t>        SkeletonSignature;
    };
} // namespace Desert::Assets::Serialization