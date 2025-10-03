#include "Mesh.hpp"

#include <assimp/Importer.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Desert
{
    namespace
    {

        glm::mat4 Mat4FromAssimpMat4( const aiMatrix4x4& matrix )
        {
            glm::mat4 result;
            // the a,b,c,d in assimp is the row ; the 1,2,3,4 is the column
            result[0][0] = matrix.a1;
            result[1][0] = matrix.a2;
            result[2][0] = matrix.a3;
            result[3][0] = matrix.a4;
            result[0][1] = matrix.b1;
            result[1][1] = matrix.b2;
            result[2][1] = matrix.b3;
            result[3][1] = matrix.b4;
            result[0][2] = matrix.c1;
            result[1][2] = matrix.c2;
            result[2][2] = matrix.c3;
            result[3][2] = matrix.c4;
            result[0][3] = matrix.d1;
            result[1][3] = matrix.d2;
            result[2][3] = matrix.d3;
            result[3][3] = matrix.d4;
            return result;
        }

        using ReturnType = std::pair<const aiScene*, std::unique_ptr<Assimp::Importer>>;
        Common::ResultStr<ReturnType>
        InitializeFromAssetData( const std::shared_ptr<Assets::MeshAsset>& meshAsset )
        {
            std::unique_ptr<Assimp::Importer> importer = std::make_unique<Assimp::Importer>();
            const auto&                       rawData  = meshAsset->GetRawData();

            static const uint32_t s_MeshImportFlags =
                 aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_SortByPType |
                 aiProcess_GenNormals | aiProcess_GenUVCoords | aiProcess_OptimizeMeshes |
                 aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights | aiProcess_ValidateDataStructure |
                 aiProcess_GlobalScale;

            auto scene = importer->ReadFileFromMemory( rawData.data(), rawData.size(), s_MeshImportFlags );
            if ( !scene )
            {
                 return Common::MakeError<ReturnType>( std::string( importer->GetErrorString() ) );
            }

            return Common::MakeSuccess( std::make_pair( scene, std::move( importer ) ) );
        }

    } // namespace
  
    class ErrorLogStream : public Assimp::LogStream
    {
    public:
        void write( const char* message ) override
        {
            LOG_ERROR( "Assimp: {}", message );
        }
    };

    class WarnLogStream : public Assimp::LogStream
    {
    public:
        void write( const char* message ) override
        {
            LOG_WARN( "Assimp: {}", message );
        }
    };

    class InfoLogStream : public Assimp::LogStream
    {
    public:
        void write( const char* message ) override
        {
            LOG_INFO( "Assimp: {}", message );
        }
    };

    class DebugLogStream : public Assimp::LogStream
    {
    public:
        void write( const char* message ) override
        {
            LOG_DEBUG( "Assimp: {}", message );
        }
    };

    class AssimpLogger
    {
    public:
        static void Init()
        {
            using namespace Assimp;

            // Create logger with verbose output
            DefaultLogger::create( "", Logger::VERBOSE );
            auto* logger = DefaultLogger::get();

            // Attach separate streams for each severity
            logger->attachStream( new ErrorLogStream, Logger::Err );
            logger->attachStream( new WarnLogStream, Logger::Warn );
            logger->attachStream( new InfoLogStream, Logger::Info );
            logger->attachStream( new DebugLogStream, Logger::Debugging );
        }

        static void Shutdown()
        {
            Assimp::DefaultLogger::kill();
        }
    };

    static std::unique_ptr<AssimpLogger> s_LogStream = nullptr;

    // Static factory methods
    std::shared_ptr<Mesh> Mesh::CreateStatic( const std::shared_ptr<Assets::MeshAsset>& meshAsset )
    {
        return std::make_shared<StaticMesh>( meshAsset );
    }

    std::shared_ptr<Mesh> Mesh::CreateStatic( const std::vector<Vertex>& vertices,
                                              const std::vector<Index>& indices, const std::string& name )
    {
        return std::make_shared<StaticMesh>( vertices, indices, name );
    }

    std::shared_ptr<Mesh> Mesh::CreateAnimated( const std::shared_ptr<Assets::MeshAsset>& meshAsset )
    {
        return std::make_shared<AnimatedMesh>( meshAsset );
    }

    // Mesh base class implementation
    Mesh::Mesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset )
         : m_MeshAsset( meshAsset ), m_Filepath( meshAsset->GetMetadata().Filepath )
    {
        InitializeAssimpLogger();
    }

    Mesh::Mesh( const std::vector<Vertex>& vertices, const std::vector<Index>& indices, const std::string& name )
         : m_Filepath( "" ), m_Custom( true )
    {
        InitializeFromCustomData( vertices, indices, name );
    }

    void Mesh::InitializeFromCustomData( const std::vector<Vertex>& vertices, const std::vector<Index>& indices,
                                         const std::string& name )
    {
        Submesh submesh;
        submesh.Name         = name;
        submesh.VertexOffset = 0;
        submesh.VertexCount  = static_cast<uint32_t>( vertices.size() );
        submesh.IndexOffset  = 0;
        submesh.IndexCount   = static_cast<uint32_t>( indices.size() ) * 3;
        submesh.Transform    = glm::mat4( 1.0f );

        submesh.BoundingBox.Min = { FLT_MAX, FLT_MAX, FLT_MAX };
        submesh.BoundingBox.Max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

        for ( const auto& vertex : vertices )
        {
            submesh.BoundingBox.Min = glm::min( submesh.BoundingBox.Min, vertex.Position );
            submesh.BoundingBox.Max = glm::max( submesh.BoundingBox.Max, vertex.Position );
        }

        m_Submeshes.push_back( submesh );
        m_TriangleCache.resize( 1 );
        m_TriangleCache[0].reserve( indices.size() );

        for ( const auto& index : indices )
        {
            m_TriangleCache[0].emplace_back( vertices[index.V1], vertices[index.V2], vertices[index.V3] );
        }

        m_VertexBuffer =
             Graphic::VertexBuffer::Create( (void*)vertices.data(), vertices.size() * sizeof( Vertex ) );
        m_VertexBuffer->RT_Invalidate();

        m_IndexBuffer = Graphic::IndexBuffer::Create( indices.data(), indices.size() * sizeof( Index ) );
        m_IndexBuffer->RT_Invalidate();
    }

    void Mesh::InitializeAssimpLogger()
    {
        static bool initialized = false;
        if ( !initialized )
        {
            initialized = true;
        }
    }

    // StaticMesh implementation
    StaticMesh::StaticMesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset ) : Mesh( meshAsset )
    {
    }

    StaticMesh::StaticMesh( const std::vector<Vertex>& vertices, const std::vector<Index>& indices,
                            const std::string& name )
         : Mesh( vertices, indices, name )
    {
    }

    Common::BoolResultWithCodes<MeshError> StaticMesh::Invalidate()
    {
        if ( m_Custom )
        {
            return Common::MakeSuccessWithCodes<bool, MeshError>( true );
        }

        auto result = InitializeFromAssetData( m_MeshAsset.lock() );
        if ( !result.IsSuccess() )
        {
            return Common::MakeErrorWithSingleCode<bool, MeshError>( MeshError::ImportError, result.GetError() );
        }

        return ProcessAssimpScene( result.GetValue().first );
    }

    Common::BoolResultWithCodes<MeshError> StaticMesh::ProcessAssimpScene( const aiScene* scene )
    {
        std::vector<Vertex> vertices;
        std::vector<Index>  indices;

        m_Submeshes.reserve( scene->mNumMeshes );
        m_TriangleCache.resize( scene->mNumMeshes );

        for ( uint32_t meshIdx = 0; meshIdx < scene->mNumMeshes; meshIdx++ )
        {
            aiMesh*  mesh        = scene->mMeshes[meshIdx];
            Submesh& submesh     = m_Submeshes.emplace_back();
            submesh.Name         = mesh->mName.C_Str();
            submesh.VertexOffset = static_cast<uint32_t>( vertices.size() );
            submesh.IndexOffset  = static_cast<uint32_t>( indices.size() );
            submesh.VertexCount  = mesh->mNumVertices;
            submesh.IndexCount   = mesh->mNumFaces * 3;

            aiNode* meshNode = scene->mRootNode->FindNode( mesh->mName );
            if ( meshNode )
            {
                aiMatrix4x4 transform = meshNode->mTransformation;
                submesh.Transform     = Mat4FromAssimpMat4( transform );
            }

            m_TriangleCache[meshIdx].reserve( mesh->mNumFaces );

            auto& aabb = submesh.BoundingBox;

            aabb.Min = { FLT_MAX, FLT_MAX, FLT_MAX };
            aabb.Max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

            for ( unsigned int vertexIdx = 0; vertexIdx < mesh->mNumVertices; vertexIdx++ )
            {
                Vertex vertex;
                vertex.Position = { mesh->mVertices[vertexIdx].x, mesh->mVertices[vertexIdx].y,
                                    mesh->mVertices[vertexIdx].z };
                vertex.Normal   = { mesh->mNormals[vertexIdx].x, mesh->mNormals[vertexIdx].y,
                                    mesh->mNormals[vertexIdx].z };

                if ( mesh->HasTangentsAndBitangents() )
                {
                    vertex.Tangent   = { mesh->mTangents[vertexIdx].x, mesh->mTangents[vertexIdx].y,
                                         mesh->mTangents[vertexIdx].z };
                    vertex.Bitangent = { mesh->mBitangents[vertexIdx].x, mesh->mBitangents[vertexIdx].y,
                                         mesh->mBitangents[vertexIdx].z };
                }

                if ( mesh->HasTextureCoords( 0 ) )
                {
                    vertex.TexCoord = { mesh->mTextureCoords[0][vertexIdx].x,
                                        mesh->mTextureCoords[0][vertexIdx].y };
                }

                glm::vec3 position = { mesh->mVertices[vertexIdx].x, mesh->mVertices[vertexIdx].y,
                                       mesh->mVertices[vertexIdx].z };

                aabb.Min = glm::min( aabb.Min, position );
                aabb.Max = glm::max( aabb.Max, position );

                vertices.push_back( vertex );
            }

            for ( unsigned int meshIndex = 0; meshIndex < mesh->mNumFaces; meshIndex++ )
            {
                const auto& index = indices.emplace_back( mesh->mFaces[meshIndex].mIndices[0],
                                                          mesh->mFaces[meshIndex].mIndices[1],
                                                          mesh->mFaces[meshIndex].mIndices[2] );

                m_TriangleCache[meshIdx].emplace_back( vertices[submesh.VertexOffset + index.V1],
                                                       vertices[submesh.VertexOffset + index.V2],
                                                       vertices[submesh.VertexOffset + index.V3] );
            }
        }

        m_VertexBuffer = Graphic::VertexBuffer::Create( vertices.data(), vertices.size() * sizeof( Vertex ) );
        m_VertexBuffer->RT_Invalidate();

        m_IndexBuffer = Graphic::IndexBuffer::Create( indices.data(), indices.size() * sizeof( Index ) );
        m_IndexBuffer->RT_Invalidate();

        return Common::MakeSuccessWithCodes<bool, MeshError>( true );
    }

    // AnimatedMesh implementation
    AnimatedMesh::AnimatedMesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset ) : Mesh( meshAsset )
    {
    }

    Common::BoolResultWithCodes<MeshError> AnimatedMesh::Invalidate()
    {
        auto result = InitializeFromAssetData( m_MeshAsset.lock() );
        if ( !result.IsSuccess() )
        {
            return Common::MakeErrorWithSingleCode<bool, MeshError>( MeshError::ImportError, result.GetError() );
            ;
        }

        return ProcessAssimpScene( result.GetValue().first );
    }

    Common::BoolResultWithCodes<MeshError> AnimatedMesh::ProcessAssimpScene( const aiScene* scene )
    {
        return Common::MakeSuccessWithCodes<bool, MeshError>( true );
    }

} // namespace Desert