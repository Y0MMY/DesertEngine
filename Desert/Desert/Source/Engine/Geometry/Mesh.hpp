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
        Animated
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
        static std::shared_ptr<Mesh> CreateAnimated( const std::shared_ptr<Assets::MeshAsset>& meshAsset );

        // Common interface
        [[nodiscard]] virtual MeshType GetType() const = 0;
        [[nodiscard]] bool             IsAnimated() const
        {
            return GetType() == MeshType::Animated;
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

    class AnimatedMesh : public Mesh
    {
    public:
        explicit AnimatedMesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset );

        [[nodiscard]] MeshType GetType() const override
        {
            return MeshType::Animated;
        }
        [[nodiscard]] Common::BoolResultWithCodes<MeshError> Invalidate() override;

        // Animation-specific methods
        [[nodiscard]] const auto& GetBoneData() const
        {
            return m_BoneData;
        }
        [[nodiscard]] const auto& GetAnimationData() const
        {
            return m_AnimationData;
        }

    private:
        struct BoneData
        {
            // Bone data implementation
        };

        struct AnimationData
        {
            // Animation data implementation
        };

        Common::BoolResultWithCodes<MeshError> ProcessAssimpScene( const aiScene* scene );

        std::vector<BoneData>      m_BoneData;
        std::vector<AnimationData> m_AnimationData;
    };
} // namespace Desert