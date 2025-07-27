#include <Engine/Graphic/Geometry/Mesh.hpp>

#include <assimp/Importer.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Desert
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

    class LogStream : public Assimp::LogStream
    {
    public:
        static void Init()
        {
            using namespace Assimp;
            const unsigned int severity = Logger::Debugging | Logger::Info | Logger::Err | Logger::Warn;

            DefaultLogger::create( "", Logger::VERBOSE );
            Assimp::DefaultLogger::get()->attachStream( new LogStream, severity );
        }

        // Write something using your own functionality
        void write( const char* message ) override
        {
            LOG_ERROR( "Assimp error: {}", message );
        }
    };

    static std::unique_ptr<LogStream> s_LogStream = nullptr;

    Mesh::Mesh( const std::string& filename ) : m_Filename( filename )
    {
        if ( !s_LogStream )
        {
            ( s_LogStream = std::make_unique<LogStream>() )->Init();
        }
    }

    Common::BoolResult Mesh::Invalidate()
    {
        const Common::Filepath path = m_Filename;

        std::unique_ptr<Assimp::Importer> importer = std::make_unique<Assimp::Importer>();

        auto scene = importer->ReadFile( path.string(), aiProcess_CalcTangentSpace | aiProcess_Triangulate |
                                                             aiProcess_SortByPType | aiProcess_GenNormals |
                                                             aiProcess_GenUVCoords | aiProcess_OptimizeMeshes |
                                                             aiProcess_ValidateDataStructure );
        if ( scene == nullptr )
        {
            LOG_ERROR( "An error occurred during mesh extraction: {}", std::string( importer->GetErrorString() ) );
            return Common::MakeError( std::string( importer->GetErrorString() ) );
        }

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

        return Common::MakeSuccess( true );
    }

} // namespace Desert