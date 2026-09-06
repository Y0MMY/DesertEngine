#include <Engine/Assets/Skybox/SkyboxAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Assets
{
    SkyboxAsset::SkyboxAsset( AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, AssetTypeID::Skybox )
    {
    }

    Common::BoolResultStr SkyboxAsset::Load()
    {
        // THE PANORAMA IS NOT READ HERE, and that is by design: EnvironmentManager::Create uploads the
        // .hdr and runs the radiance/irradiance/prefilter bakes on the GPU, so this asset holds nothing
        // but the identity of a file. What it OWES its caller, therefore, is the one thing it can
        // establish without a device — that the file it names is actually there.
        //
        // It used to owe nothing: the body was `m_ReadyForUse = true; return BOOLSUCCESS;` with the only
        // check commented out (against an m_TextureAsset member this class does not have). A skybox
        // whose .hdr had been moved, renamed or left out of a package therefore loaded, registered,
        // and reported ready — and the sky came out black with every diagnostic in the editor saying
        // the skybox was fine. Exists() asks the mounted .dpak as well as the disk, so a packaged game
        // answers this identically.
        if ( !Common::Utils::FileSystem::Exists( m_Metadata.Filepath ) )
        {
            m_ReadyForUse = false;
            LOG_ERROR( "[SkyboxAsset] '{}' is not on disk and not in a mounted pak — the skybox is not "
                       "loaded and the scene has no environment from it.",
                       m_Metadata.Filepath.string() );
            return Common::MakeFormattedError( "skybox '{}' is not on disk and not in a mounted pak",
                                               m_Metadata.Filepath.string() );
        }

        m_ReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr SkyboxAsset::Unload()
    {
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets