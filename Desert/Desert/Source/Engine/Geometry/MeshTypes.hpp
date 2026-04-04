#pragma once

#include <glm/glm.hpp>
#include <Common/Core/Math/AABB.hpp>

#include <array>

namespace Desert
{
    struct Vertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec3 Tangent;
        glm::vec3 Bitangent;
        glm::vec2 TexCoord;
    };

    struct SkinnedVertex
    {
        static constexpr size_t MAX_BONE_INFLUENCES = 4;

        Vertex StaticVertex;

        std::array<uint32_t, MAX_BONE_INFLUENCES> BoneIDs     = { 0, 0, 0, 0 };
        std::array<float, MAX_BONE_INFLUENCES>    BoneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };

        bool AddBoneInfluence( uint32_t boneID, float weight )
        {
            for ( size_t i = 0; i < MAX_BONE_INFLUENCES; i++ )
            {
                if ( BoneWeights[i] == 0.0f )
                {
                    BoneIDs[i]     = boneID;
                    BoneWeights[i] = weight;
                    return true;
                }
            }

            size_t smallestIdx = 0;
            for ( size_t i = 1; i < MAX_BONE_INFLUENCES; i++ )
            {
                if ( BoneWeights[i] < BoneWeights[smallestIdx] )
                {
                    smallestIdx = i;
                }
            }

            if ( weight > BoneWeights[smallestIdx] )
            {
                BoneIDs[smallestIdx]     = boneID;
                BoneWeights[smallestIdx] = weight;
                NormalizeWeights();
                return true;
            }

            return false;
        }

        void NormalizeWeights()
        {
            float sum = 0.0f;
            for ( float weight : BoneWeights )
            {
                sum += weight;
            }

            if ( sum > 0.0f )
            {
                float invSum = 1.0f / sum;
                for ( float& weight : BoneWeights )
                {
                    weight *= invSum;
                }
            }
        }

        bool HasFreeSlot() const
        {
            for ( float weight : BoneWeights )
            {
                if ( weight == 0.0f )
                    return true;
            }
            return false;
        }

        size_t GetActiveInfluenceCount() const
        {
            size_t count = 0;
            for ( float weight : BoneWeights )
            {
                if ( weight > 0.0f )
                    count++;
            }
            return count;
        }
    };

    struct Index
    {
        uint32_t V1, V2, V3;
    };

    struct TriangleCache
    {
        Vertex V1, V2, V3;
    };

    struct Submesh
    {
        std::string        Name;
        uint32_t           VertexOffset;
        uint32_t           VertexCount;
        uint32_t           IndexOffset;
        uint32_t           IndexCount;
        glm::mat4          Transform;
        Common::Math::AABB BoundingBox;
    };

    enum class MeshType
    {
        Static,
        Skinned
    };
} // namespace Desert