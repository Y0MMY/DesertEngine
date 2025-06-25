#include <Engine/Assets/Mesh/MaterialAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <assimp/Importer.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Desert::Assets
{

    static Common::Filepath GetMaterialFilename( const Common::Filepath& filepath )
    {
        const auto materialPath = Common::Constants::Path::MATERIAL_PATH.string() +
                                  Common::Utils::FileSystem::GetFileNameWithoutExtension( filepath );

        return materialPath + Common::Constants::Extensions::MATERIAL_EXTENSION;
    }

    MaterialAsset::MaterialAsset( const AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath ), m_MaterialAssetPath( GetMaterialFilename( filepath ) )
    {
    }

    Common::BoolResult MaterialAsset::Load()
    {
        const std::string modelPath = m_Filepath.string();
        const std::string directory = Common::Utils::FileSystem::GetFileDirectoryString( m_Filepath );

        Assimp::Importer importer;
        const aiScene*   scene = importer.ReadFile( modelPath, 0 );

        if ( !scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode ||
             scene->mNumMaterials == 0 )
        {
            LOG_ERROR( "Failed to load model: {}", std::string( importer.GetErrorString() ) );
            return Common::MakeError( "Failed to load model: " + std::string( importer.GetErrorString() ) );
        }

        aiMaterial* material = scene->mMaterials[0];

        auto loadTexture = [&]( aiTextureType type, TextureAsset::Type textureType,
                                const glm::vec4& defaultColor ) -> bool
        {
            if ( material->GetTextureCount( type ) > 0 )
            {
                aiString texturePath;
                if ( material->GetTexture( type, 0, &texturePath ) == AI_SUCCESS )
                {
                    const Common::Filepath fullPath = directory + texturePath.C_Str();

                    auto textureSlot = std::make_unique<TextureSlot>();
                    textureSlot->Texture =
                         std::make_unique<TextureAsset>( AssetPriority::Low, fullPath, textureType );
                    textureSlot->DefaultColor = defaultColor;

                    if ( textureSlot->Texture->Load() )
                    {
                        m_TextureSlots.push_back( std::move( textureSlot ) );
                        m_TextureLookup[textureType] = std::prev( m_TextureSlots.end() );
                        return true;
                    }
                }
            }
            return false;
        };

        loadTexture( aiTextureType_DIFFUSE, TextureAsset::Type::Albedo, glm::vec4( 1.0f ) );
        loadTexture( aiTextureType_NORMALS, TextureAsset::Type::Normal, glm::vec4( 0.5f, 0.5f, 1.0f, 1.0f ) );
        loadTexture( aiTextureType_METALNESS, TextureAsset::Type::Metallic, glm::vec4( 0.0f ) );
        loadTexture( aiTextureType_DIFFUSE_ROUGHNESS, TextureAsset::Type::Roughness, glm::vec4( 1.0f ) );
        loadTexture( aiTextureType_AMBIENT_OCCLUSION, TextureAsset::Type::AO, glm::vec4( 1.0f ) );
        loadTexture( aiTextureType_EMISSIVE, TextureAsset::Type::Emissive, glm::vec4( 0.0f ) );

        m_ReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResult MaterialAsset::Unload()
    {
        return BOOLSUCCESS;
    }

    AssetManager::KeyHandle MaterialAsset::GetAssetKey( const Common::Filepath& filepath )
    {
        return GetMaterialFilename( filepath );
    }

} // namespace Desert::Assets