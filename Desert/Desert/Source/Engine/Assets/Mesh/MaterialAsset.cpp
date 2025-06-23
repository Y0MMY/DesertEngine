#include <Engine/Assets/Mesh/MaterialAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <assimp/Importer.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Desert::Assets
{
    Common::BoolResult MaterialAsset::Load()
    {
        const std::string modelPath = m_Filepath.string();
        const std::string directory = Common::Utils::FileSystem::GetFileDirectoryString( m_Filepath );

        Assimp::Importer importer;

        const aiScene* scene = importer.ReadFile( modelPath, 0 );

        if ( !scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode ||
             scene->mNumMaterials == 0 )
        {
            LOG_ERROR( "Failed to load model: {}", std::string( importer.GetErrorString() ) );
            return Common::MakeError( "Failed to load model: " + std::string( importer.GetErrorString() ) );
        }

        aiMaterial* material = scene->mMaterials[0];

        auto loadTexture = [&]( aiTextureType type, MaterialTextureName textureName ) -> bool
        {
            if ( material->GetTextureCount( type ) > 0 )
            {
                aiString texturePath;
                if ( material->GetTexture( type, 0, &texturePath ) == AI_SUCCESS )
                {
                    const Common::Filepath fullPath = directory + texturePath.C_Str();

                    auto texture = Graphic::Texture2D::Create( { true }, fullPath );
                    if ( texture->Invalidate() )
                    {
                        m_MaterialsTexture[static_cast<uint8_t>( textureName )] = { fullPath, texture };
                        return true;
                    }
                }
            }
            return false;
        };

        bool hasAlbedo    = loadTexture( aiTextureType_DIFFUSE, MaterialTextureName::Albedo );
        bool hasMetallic  = loadTexture( aiTextureType_METALNESS, MaterialTextureName::Metallic );
        bool hasRoughness = loadTexture( aiTextureType_DIFFUSE_ROUGHNESS, MaterialTextureName::Roughness );

        LOG_TRACE( "Albedo was{}load", hasAlbedo ? " " : " not " )
        LOG_TRACE( "Metallic was{}load", hasMetallic ? " " : " not " )
        LOG_TRACE( "Roughness was{}load", hasRoughness ? " " : " not " )

        m_ReadyForUse = true;

        return BOOLSUCCESS;
    }

    Common::BoolResult MaterialAsset::Unload()
    {
        return BOOLSUCCESS;
    }

} // namespace Desert::Assets