#include <Engine/Graphic/Geometry/Mesh.hpp>

#include <assimp/Importer.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Desert
{
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
        const Common::Filepath path = Common::Constants::Path::MESH_PATH / m_Filename;

        std::unique_ptr<Assimp::Importer> importer = std::make_unique<Assimp::Importer>();

        auto scene = importer->ReadFile( path.string(), aiProcess_CalcTangentSpace | aiProcess_Triangulate |
                                                             aiProcess_SortByPType | aiProcess_GenNormals |
                                                             aiProcess_GenUVCoords | aiProcess_OptimizeMeshes |
                                                             aiProcess_ValidateDataStructure );
        if ( scene == nullptr )
        {
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

            m_TriangleCache[meshIdx].reserve( mesh->mNumFaces );

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