#include <Engine/Assets/Skybox/SkyboxAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Assets
{
    SkyboxAsset::SkyboxAsset( AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, AssetTypeID::Skybox )
    {
    }

    Common::BoolResult SkyboxAsset::Load()
    {
        m_TextureAsset =
             std::make_unique<TextureAsset>( AssetPriority::Low, m_Metadata.Filepath, TextureAsset::Type::Skybox );

        if ( !m_TextureAsset->Load() )
        {
            LOG_WARN( "Failed to load texture: {}", m_Metadata.Filepath.string() );
            return Common::MakeFormattedError( "Failed to load texture: {}", m_Metadata.Filepath.string() );
        }

        m_Environment = Graphic::EnvironmentManager::Create( m_Metadata.Filepath.string() ); // TODO: param:
                                                                                             // texture
        m_ReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResult SkyboxAsset::Unload()
    {
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets