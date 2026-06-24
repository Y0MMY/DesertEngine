#include "AssimpImporter.hpp"

#include <limits>

#include <assimp/Importer.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <Engine/Assets/Serialization/Mesh.hpp>
#include <Engine/Assets/Serialization/Skeleton.hpp>
#include <Engine/Assets/Serialization/Animation.hpp>
#include <Engine/Assets/Serialization/Material.hpp>

#include <Common/Core/Logger.hpp>

#include <Engine/Animation/Skeleton.hpp>

#include <Editor/Import/ImportManager.hpp>

struct aiNode;
struct aiAnimation;
struct aiNodeAnim;
struct aiScene;

namespace Assimp
{
    class Importer;
}

namespace Desert::Editor
{
    class ScopedAssimpLogger
    {
    public:
        ScopedAssimpLogger()
        {
            Assimp::DefaultLogger::create( "", Assimp::Logger::VERBOSE );
            auto* logger = Assimp::DefaultLogger::get();

            logger->attachStream( new ErrorStream, Assimp::Logger::Err );
            logger->attachStream( new WarnStream, Assimp::Logger::Warn );
            logger->attachStream( new InfoStream, Assimp::Logger::Info );
            logger->attachStream( new DebugStream, Assimp::Logger::DEBUGGING );
        }

        ~ScopedAssimpLogger()
        {
            Assimp::DefaultLogger::kill();
        }

    private:
        class ErrorStream : public Assimp::LogStream
        {
        public:
            void write( const char* message ) override
            {
                LOG_ERROR( "Assimp: {}", message );
            }
        };

        class WarnStream : public Assimp::LogStream
        {
        public:
            void write( const char* message ) override
            {
                LOG_WARN( "Assimp: {}", message );
            }
        };

        class InfoStream : public Assimp::LogStream
        {
        public:
            void write( const char* message ) override
            {
                LOG_INFO( "Assimp: {}", message );
            }
        };

        class DebugStream : public Assimp::LogStream
        {
        public:
            void write( const char* message ) override
            {
                LOG_DEBUG( "Assimp: {}", message );
            }
        };
    };

    using namespace Desert::Assets::Serialization;

    static glm::mat4 ConvertMatrix( const aiMatrix4x4& matrix )
    {
        glm::mat4 result;
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

    static aiNode* FindNodeRecursive( aiNode* node, const std::string& name )
    {
        if ( name == node->mName.C_Str() )
            return node;

        for ( uint32_t i = 0; i < node->mNumChildren; ++i )
        {
            if ( auto* found = FindNodeRecursive( node->mChildren[i], name ) )
                return found;
        }

        return nullptr;
    }

    static glm::mat4 GetFullLocalTransform( aiNode*                                          node,
                                            const std::unordered_map<std::string, uint32_t>& boneMapping )
    {
        glm::mat4 transform = ConvertMatrix( node->mTransformation );

        aiNode* parent = node->mParent;

        while ( parent )
        {
            if ( boneMapping.contains( parent->mName.C_Str() ) )
                break;

            transform = ConvertMatrix( parent->mTransformation ) * transform;
            parent    = parent->mParent;
        }

        return transform;
    }

    static void BuildSkeletonHierarchy( aiNode* root, std::unordered_map<std::string, uint32_t>& boneMapping,
                                        SkeletonAssetData& skeletonData )
    {
        for ( auto& [boneName, boneIndex] : boneMapping )
        {
            aiNode* node = FindNodeRecursive( root, boneName );
            if ( !node )
                continue;

            aiNode* parentNode = node->mParent;

            auto& bone = skeletonData.Bones[boneIndex];

            if ( parentNode )
            {
                auto it = boneMapping.find( parentNode->mName.C_Str() );

                if ( it != boneMapping.end() )
                {
                    bone.ParentBoneID = it->second;
                }
                else
                {
                    bone.ParentBoneID = std::nullopt;
                }
            }
            else
            {
                bone.ParentBoneID = std::nullopt;
            }

            bone.LocalBindTransform = GetFullLocalTransform( node, boneMapping );
        }
    }

    static glm::vec4 GetColor( aiMaterial* mat, const char* key, unsigned type, unsigned idx, glm::vec4 def )
    {
        aiColor4D color;
        if ( aiGetMaterialColor( mat, key, type, idx, &color ) == AI_SUCCESS )
        {
            return { color.r, color.g, color.b, color.a };
        }
        return def;
    }

    static float GetFloat( aiMaterial* mat, const char* key, unsigned type, unsigned idx, float def )
    {
        float value;
        if ( aiGetMaterialFloat( mat, key, type, idx, &value ) == AI_SUCCESS )
            return value;
        return def;
    }

    static std::vector<MaterialAssetData>
    ExtractMaterials( const aiScene* scene, const std::filesystem::path& basePath, ImportManager& manager )
    {
        std::vector<MaterialAssetData> result;

        for ( uint32_t i = 0; i < scene->mNumMaterials; ++i )
        {
            aiMaterial* mat = scene->mMaterials[i];

            MaterialAssetData data;

            aiString name;
            mat->Get( AI_MATKEY_NAME, name );
            data.Name           = name.C_Str();
            data.MaterialHandle = Common::UUID{};

            auto loadTex = [&]( aiTextureType type ) -> std::optional<TextureRef>
            {
                if ( mat->GetTextureCount( type ) == 0 )
                    return std::nullopt;

                aiString path;
                if ( mat->GetTexture( type, 0, &path ) != AI_SUCCESS )
                    return std::nullopt;

                TextureRef            ref;
                std::filesystem::path assimpPath = path.C_Str();
                std::filesystem::path finalPath;

                if ( !assimpPath.is_absolute() )
                {
                    finalPath = ( basePath / assimpPath ).lexically_normal();
                }
                else
                {
                    finalPath = basePath / assimpPath.filename();
                }

                ref.Path   = finalPath.string();
                ref.Handle = manager.ImportTexture( ref.Path );
                return ref;
            };

            // TEXTURES
            data.Albedo.Texture    = loadTex( aiTextureType_DIFFUSE );
            data.Metallic.Texture  = loadTex( aiTextureType_METALNESS );
            data.Roughness.Texture = loadTex( aiTextureType_DIFFUSE_ROUGHNESS );
            data.AO.Texture        = loadTex( aiTextureType_AMBIENT_OCCLUSION );
            data.Emissive.Texture  = loadTex( aiTextureType_EMISSIVE );

            data.Albedo.Value    = GetColor( mat, AI_MATKEY_COLOR_DIFFUSE, glm::vec4( 1.0f ) );
            data.Metallic.Value  = GetFloat( mat, AI_MATKEY_METALLIC_FACTOR, 0.0f );
            data.Roughness.Value = GetFloat( mat, AI_MATKEY_ROUGHNESS_FACTOR, 1.0f );
            auto emissive        = GetColor( mat, AI_MATKEY_COLOR_EMISSIVE, glm::vec4( 0.0f ) );
            data.Emissive.Value  = glm::vec3( emissive );

            result.push_back( data );
        }

        return result;
    }

    static ImportResult ProcessScene( const aiScene* scene, ImportManager& manager, const std::string& assetName )
    {
        ImportResult result;

        MeshAssetData     meshData;
        SkeletonAssetData skeletonData;

        const auto materialData =
             ExtractMaterials( scene, ( std::filesystem::path( "Resources/Textures/" ) / assetName ), manager );

        std::unordered_map<std::string, uint32_t> boneMapping;

        bool hasBones = false;

        // ============================
        // Mesh extraction
        // ============================

        for ( uint32_t meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx )
        {
            aiMesh* mesh = scene->mMeshes[meshIdx];

            bool meshHasBones = mesh->HasBones();
            hasBones |= meshHasBones;

            SubmeshData submesh;
            submesh.Name = mesh->mName.C_Str();

            submesh.VertexOffset = meshHasBones ? meshData.SkinnedVertices.size() : meshData.StaticVertices.size();

            submesh.IndexOffset    = meshData.Indices.size() * 3;
            submesh.VertexCount    = mesh->mNumVertices;
            submesh.IndexCount     = mesh->mNumFaces * 3;
            submesh.Transform      = glm::mat4( 1.0f );
            submesh.MaterialHandle = materialData[mesh->mMaterialIndex].MaterialHandle;

            // ============================
            // VERTICES
            // ============================

            if ( !meshHasBones )
            {
                // -------- STATIC --------
                for ( uint32_t i = 0; i < mesh->mNumVertices; ++i )
                {
                    StaticVertexData v{};

                    v.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

                    if ( mesh->HasNormals() )
                        v.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };

                    if ( mesh->HasTangentsAndBitangents() )
                    {
                        v.Tangent   = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
                        v.Bitangent = { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z };
                    }

                    if ( mesh->HasTextureCoords( 0 ) )
                        v.TexCoord = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };

                    meshData.StaticVertices.push_back( v );
                }
            }
            else
            {
                // -------- SKINNED --------
                std::vector<SkinnedVertexData> tempVertices( mesh->mNumVertices );

                for ( uint32_t i = 0; i < mesh->mNumVertices; ++i )
                {
                    auto& v = tempVertices[i];

                    v.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

                    if ( mesh->HasNormals() )
                        v.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };

                    if ( mesh->HasTangentsAndBitangents() )
                    {
                        v.Tangent   = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
                        v.Bitangent = { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z };
                    }

                    if ( mesh->HasTextureCoords( 0 ) )
                        v.TexCoord = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
                }

                // ============================
                // BONES + WEIGHTS
                // ============================

                for ( uint32_t b = 0; b < mesh->mNumBones; ++b )
                {
                    aiBone*     aiBone = mesh->mBones[b];
                    std::string name   = aiBone->mName.C_Str();

                    uint32_t boneIndex;

                    if ( !boneMapping.contains( name ) )
                    {
                        boneIndex         = static_cast<uint32_t>( skeletonData.Bones.size() );
                        boneMapping[name] = boneIndex;

                        Desert::Animation::BoneInfo bone;
                        bone.Name               = name;
                        bone.BoneIndex          = boneIndex;
                        bone.OffsetMatrix       = ConvertMatrix( aiBone->mOffsetMatrix );
                        bone.LocalBindTransform = glm::mat4( 1.0f );

                        skeletonData.Bones.push_back( bone );
                    }
                    else
                    {
                        boneIndex = boneMapping[name];
                    }

                    for ( uint32_t w = 0; w < aiBone->mNumWeights; ++w )
                    {
                        const aiVertexWeight& weight = aiBone->mWeights[w];

                        uint32_t vertexID = weight.mVertexId;
                        float    value    = weight.mWeight;

                        auto& vertex = tempVertices[vertexID];

                        auto& v = tempVertices[vertexID];

                        int minIndex = 0;
                        for ( int i = 1; i < 4; ++i )
                        {
                            if ( v.BoneWeights[i] < v.BoneWeights[minIndex] )
                                minIndex = i;
                        }

                        if ( value > v.BoneWeights[minIndex] )
                        {
                            v.BoneIDs[minIndex]     = boneIndex;
                            v.BoneWeights[minIndex] = value;
                        }
                    }
                }

                //// (optional) normalize weights
                // for ( auto& v : tempVertices )
                //{
                //     float sum = v.BoneWeights[0] + v.BoneWeights[1] + v.BoneWeights[2] + v.BoneWeights[3];

                //    if ( sum > 0.0f )
                //    {
                //        for ( int i = 0; i < 4; ++i )
                //        {
                //            v.BoneWeights[i] /= sum;
                //        }
                //    }
                //}

                for ( auto& v : tempVertices )
                {
                    meshData.SkinnedVertices.push_back( v );
                }
            }

            // ============================
            // INDICES
            // ============================

            for ( uint32_t i = 0; i < mesh->mNumFaces; ++i )
            {
                auto& face = mesh->mFaces[i];

                meshData.Indices.push_back( { face.mIndices[0], face.mIndices[1], face.mIndices[2] } );
            }

            // ============================
            // BOUNDING BOX
            // ============================

            if ( mesh->mNumVertices > 0 )
            {
                glm::vec3 aabbMin( std::numeric_limits<float>::max() );
                glm::vec3 aabbMax( -std::numeric_limits<float>::max() );

                for ( uint32_t i = 0; i < mesh->mNumVertices; ++i )
                {
                    const glm::vec3 pos = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
                    aabbMin             = glm::min( aabbMin, pos );
                    aabbMax             = glm::max( aabbMax, pos );
                }

                submesh.BoundingBox = { aabbMin, aabbMax };
            }

            meshData.Submeshes.push_back( submesh );
        }

        // ============================
        // FINALIZE
        // ============================

        meshData.IsSkinned = hasBones;

        if ( hasBones )
        {

            BuildSkeletonHierarchy( scene->mRootNode, boneMapping, skeletonData );

            skeletonData.Signature     = Animation::Skeleton::ComputeSignature( skeletonData.Bones );
            meshData.SkeletonSignature = skeletonData.Signature;
        }

        if ( !meshData.StaticVertices.empty() || !meshData.SkinnedVertices.empty() )
            result.Mesh = meshData;

        if ( hasBones )
            result.Skeleton = skeletonData;

        // ============================
        // ANIMATIONS (��� ���������)
        // ============================

        for ( uint32_t i = 0; i < scene->mNumAnimations; ++i )
        {
            aiAnimation* anim = scene->mAnimations[i];

            AnimationAssetData animData;
            animData.Name = anim->mName.length > 0 ? anim->mName.C_Str() : "Animation_" + std::to_string( i );

            animData.Duration       = anim->mDuration;
            animData.TicksPerSecond = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0f;

            animData.SkeletonSignature = skeletonData.Signature;

            for ( uint32_t c = 0; c < anim->mNumChannels; ++c )
            {
                aiNodeAnim* channel = anim->mChannels[c];

                ChannelData ch;
                ch.BoneName = channel->mNodeName.C_Str();
                auto it     = boneMapping.find( channel->mNodeName.C_Str() );
                if ( it == boneMapping.end() )
                    continue;

                ch.BoneIndex = it->second;

                for ( uint32_t p = 0; p < channel->mNumPositionKeys; ++p )
                {
                    ch.Positions.push_back(
                         { (float)channel->mPositionKeys[p].mTime,
                           { channel->mPositionKeys[p].mValue.x, channel->mPositionKeys[p].mValue.y,
                             channel->mPositionKeys[p].mValue.z } } );
                }

                for ( uint32_t r = 0; r < channel->mNumRotationKeys; ++r )
                {
                    auto& q = channel->mRotationKeys[r].mValue;

                    ch.Rotations.push_back(
                         { (float)channel->mRotationKeys[r].mTime, glm::quat( q.w, q.x, q.y, q.z ) } );
                }

                for ( uint32_t s = 0; s < channel->mNumScalingKeys; ++s )
                {
                    ch.Scales.push_back( { (float)channel->mScalingKeys[s].mTime,
                                           { channel->mScalingKeys[s].mValue.x, channel->mScalingKeys[s].mValue.y,
                                             channel->mScalingKeys[s].mValue.z } } );
                }

                animData.Channels.push_back( ch );
            }

            result.Animations.push_back( animData );
        }

        result.Material = materialData;

        return result;
    }

    ImportResult AssimpImporter::Import( const std::filesystem::path& path, ImportManager& manager )
    {
        static ScopedAssimpLogger logger;
        Assimp::Importer          importer;

        const uint32_t flags = aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_CalcTangentSpace |
                               aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights |
                               aiProcess_GlobalScale;

        const aiScene* scene = importer.ReadFile( path.string(), flags );

        if ( !scene || !scene->mRootNode )
        {
            throw std::runtime_error( "Failed to import: " + path.string() );
        }

        return ProcessScene( scene, manager, path.stem().string() );
    }

} // namespace Desert::Editor