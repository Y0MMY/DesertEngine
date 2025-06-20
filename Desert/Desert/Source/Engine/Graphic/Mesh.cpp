#include <Engine/Graphic/Mesh.hpp>

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

        aiMesh*             mesh = scene->mMeshes[0];
        std::vector<Vertex> vertices;
        std::vector<Index>  indices;

        for ( unsigned int i = 0; i < mesh->mNumVertices; i++ )
        {
            Vertex vertex;
            vertex.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
            vertex.Normal   = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };

            if ( mesh->HasTangentsAndBitangents() )
            {
                vertex.Tangent   = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
                vertex.Bitangent = { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z };
            }

            if ( mesh->HasTextureCoords( 0 ) )
            {
                vertex.TexCoord = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
            }

            vertices.push_back( vertex );
        }

        for ( unsigned int i = 0; i < mesh->mNumFaces; i++ )
        {
            indices.emplace_back( mesh->mFaces[i].mIndices[0], mesh->mFaces[i].mIndices[1],
                                  mesh->mFaces[i].mIndices[2] );
        }

        m_VertexBuffer = Graphic::VertexBuffer::Create( vertices.data(), vertices.size() * sizeof( Vertex ) );
        m_VertexBuffer->RT_Invalidate();

        m_IndexBuffer = Graphic::IndexBuffer::Create( indices.data(), indices.size() * sizeof( Index ) );
        m_IndexBuffer->RT_Invalidate();

        return Common::MakeSuccess( true );
    }

} // namespace Desert