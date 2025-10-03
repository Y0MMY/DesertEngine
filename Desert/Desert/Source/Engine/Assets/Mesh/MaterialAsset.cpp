#include <Engine/Assets/Mesh/MaterialAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <assimp/Importer.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Desert::Assets
{
    namespace
    {
        constexpr std::array<aiTextureType, static_cast<size_t>( 6U )> kTextureTypeMapping = {
             aiTextureType_DIFFUSE,           // Albedo
             aiTextureType_NORMALS,           // Normal
             aiTextureType_METALNESS,         // Metallic
             aiTextureType_DIFFUSE_ROUGHNESS, // Roughness
             aiTextureType_AMBIENT_OCCLUSION, // AO
             aiTextureType_EMISSIVE           // Emissive
        };

        constexpr std::array<glm::vec4, static_cast<size_t>( 6U )> kDefaultColors = {
             glm::vec4( 1.0f ),                   // Albedo (white)
             glm::vec4( 0.5f, 0.5f, 1.0f, 1.0f ), // Normal (blue)
             glm::vec4( 0.0f ),                   // Metallic (black)
             glm::vec4( 1.0f ),                   // Roughness (white)
             glm::vec4( 1.0f ),                   // AO (white)
             glm::vec4( 0.0f )                    // Emissive (black)
        };
    } // namespace

    MaterialAsset::MaterialAsset( AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, AssetTypeID::Material )
    {
    }

    Common::BoolResultStr MaterialAsset::Load()
    {
        static constexpr uint32_t s_MeshImportFlags =
             aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_SortByPType | aiProcess_GenNormals |
             aiProcess_GenUVCoords | aiProcess_OptimizeMeshes | aiProcess_ValidateDataStructure;

        Assimp::Importer importer;
        const aiScene*   scene = importer.ReadFile( m_Metadata.Filepath.string(), s_MeshImportFlags );

        if ( !scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode ||
             scene->mNumMaterials == 0 )
        {
            return Common::MakeError( "Failed to load model: " + std::string( importer.GetErrorString() ) );
        }

        aiMaterial* material = scene->mMaterials[0];

        for ( size_t i = 0; i < kTextureTypeMapping.size(); ++i )
        {
            const auto type = static_cast<TextureAsset::Type>( i );
            if ( material->GetTextureCount( kTextureTypeMapping[i] ) > 0 )
            {
                aiString texturePath;
                if ( material->GetTexture( kTextureTypeMapping[i], 0, &texturePath ) == AI_SUCCESS )
                {
                    Common::Filepath fullPath =
                         Common::Utils::FileSystem::GetFileNameWithoutExtension_PATH( m_Metadata.Filepath ) /
                         texturePath.C_Str();
                    AddTexture( fullPath, type, kDefaultColors[i] );
                }
            }
        }

        m_ReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr MaterialAsset::Unload()
    {
        return BOOLSUCCESS;
    }

    std::optional<std::reference_wrapper<const MaterialAsset::TextureSlot>>
    MaterialAsset::GetTextureSlot( TextureAsset::Type type ) const
    {
        const auto& slot = m_TextureSlots[static_cast<size_t>( type )];
        if ( slot )
            return std::cref( *slot );
        return std::nullopt;
    }

    std::shared_ptr<Graphic::Texture2D> MaterialAsset::GetTexture( TextureAsset::Type type ) const
    {
        if ( auto slot = GetTextureSlot( type ) )
        {
            if ( slot->get().Texture && slot->get().Texture->IsReadyForUse() )
            {
                return slot->get().Texture->GetTexture();
            }
        }
        return nullptr;
    }

    bool MaterialAsset::AddTexture( const Common::Filepath& filepath, TextureAsset::Type type,
                                    const glm::vec4& defaultColor )
    {
        if ( m_TextureSlots[static_cast<size_t>( type )] )
        {
            LOG_WARN( "Texture slot for type {} is already occupied", static_cast<int>( type ) );
            return false;
        }

        auto textureSlot          = std::make_unique<TextureSlot>();
        textureSlot->Texture      = std::make_unique<TextureAsset>( AssetPriority::Low, filepath, type );
        textureSlot->DefaultColor = defaultColor;

        if ( !textureSlot->Texture->Load() )
        {
            LOG_WARN( "Failed to load texture: {}", filepath.string() );
            return false;
        }

        m_TextureSlots[static_cast<size_t>( type )] = std::move( textureSlot );
        return true;
    }

    bool MaterialAsset::CopyFrom( const MaterialAsset& source )
    {
        if ( !source.IsReadyForUse() )
        {
            return false;
        }

        for ( auto& slot : m_TextureSlots )
        {
            slot.reset();
        }

        for ( size_t i = 0; i < source.m_TextureSlots.size(); ++i )
        {
            if ( source.m_TextureSlots[i] )
            {
                auto newSlot          = std::make_unique<TextureSlot>();
                newSlot->DefaultColor = source.m_TextureSlots[i]->DefaultColor;

                if ( source.m_TextureSlots[i]->Texture )
                {
                    newSlot->Texture =
                         std::make_unique<TextureAsset>( source.m_TextureSlots[i]->Texture->GetMetadata().Priority,
                                                         source.m_TextureSlots[i]->Texture->GetMetadata().Filepath,
                                                         source.m_TextureSlots[i]->Texture->GetType() );
                    newSlot->Texture->Load();
                }

                m_TextureSlots[i] = std::move( newSlot );
            }
        }

        m_ReadyForUse = true;
        return true;
    }

} // namespace Desert::Assets