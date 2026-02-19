#pragma once

#include <Common/Core/Math/AABB.hpp>
#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include "Errors/MeshError.hpp"

struct aiNode;
struct aiAnimation;
struct aiNodeAnim;
struct aiScene;

namespace Desert::Animation
{
    class Skeleton;
    struct BoneInfo;
} // namespace Desert::Animation

namespace Assimp
{
    class Importer;
}

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

    class Mesh
    {
    public:
        virtual ~Mesh() = default;

        // Factory methods
        static std::shared_ptr<Mesh> CreateStatic( const std::shared_ptr<Assets::MeshAsset>& meshAsset );
        static std::shared_ptr<Mesh> CreateStatic( const std::vector<Vertex>& vertices,
                                                   const std::vector<Index>&  indices,
                                                   const std::string&         name = "CustomMesh" );

        static std::shared_ptr<Mesh> CreateSkinned( const std::shared_ptr<Assets::MeshAsset>& meshAsset );

        static bool DetectIfSkinned( const std::shared_ptr<Assets::MeshAsset>& meshAsset );

        // Common interface
        [[nodiscard]] virtual MeshType GetType() const = 0;
        [[nodiscard]] bool             IsSkinned() const
        {
            return GetType() == MeshType::Skinned;
        }

        [[nodiscard]] virtual const std::vector<Submesh>& GetSubmeshes() const
        {
            return m_Submeshes;
        }
        [[nodiscard]] const std::shared_ptr<Graphic::VertexBuffer>& GetVertexBuffer() const
        {
            return m_VertexBuffer;
        }
        [[nodiscard]] const std::shared_ptr<Graphic::IndexBuffer>& GetIndexBuffer() const
        {
            return m_IndexBuffer;
        }
        [[nodiscard]] const auto& GetAsset() const
        {
            return m_MeshAsset;
        }
        [[nodiscard]] const auto& GetFilepath() const
        {
            return m_Filepath;
        }

        [[nodiscard]] virtual Common::BoolResultWithCodes<MeshError> Invalidate() = 0;

        Mesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset );
        Mesh( const std::vector<Vertex>& vertices, const std::vector<Index>& indices, const std::string& name );

    protected:
        // Common initialization methods
        void InitializeFromCustomData( const std::vector<Vertex>& vertices, const std::vector<Index>& indices,
                                       const std::string& name );

        // Protected data accessible by derived classes
        std::shared_ptr<Graphic::VertexBuffer>  m_VertexBuffer;
        std::shared_ptr<Graphic::IndexBuffer>   m_IndexBuffer;
        std::vector<std::vector<TriangleCache>> m_TriangleCache;
        std::vector<Submesh>                    m_Submeshes;
        std::weak_ptr<Assets::MeshAsset>        m_MeshAsset;
        Common::Filepath                        m_Filepath;
        bool                                    m_Custom = false;

    private:
        static void InitializeAssimpLogger();
    };

    class StaticMesh : public Mesh
    {
    public:
        StaticMesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset );
        StaticMesh( const std::vector<Vertex>& vertices, const std::vector<Index>& indices,
                    const std::string& name = "CustomMesh" );

        [[nodiscard]] MeshType GetType() const override
        {
            return MeshType::Static;
        }
        [[nodiscard]] Common::BoolResultWithCodes<MeshError> Invalidate() override;

    private:
        Common::BoolResultWithCodes<MeshError> ProcessAssimpScene( const aiScene* scene );
    };

    class SkinnedMesh : public Mesh
    {
    public:
        explicit SkinnedMesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset );

        [[nodiscard]] MeshType GetType() const override
        {
            return MeshType::Skinned;
        }

        [[nodiscard]] Common::BoolResultWithCodes<MeshError> Invalidate() override;

        // Skinned-specific methods
        const Animation::Skeleton& GetSkeleton() const
        {
            return *m_Skeleton;
        }

        const std::unordered_map<std::string, uint32_t>& GetBoneMapping() const
        {
            return m_BoneNameToIndex;
        }

    private:
        Common::BoolResultWithCodes<MeshError> ProcessAssimpScene( const aiScene* scene );

    private:
        std::unique_ptr<Animation::Skeleton>      m_Skeleton;
        std::unordered_map<std::string, uint32_t> m_BoneNameToIndex;
    };
} // namespace Desert
