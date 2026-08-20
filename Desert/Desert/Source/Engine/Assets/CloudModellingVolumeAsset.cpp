#include <Engine/Assets/CloudModellingVolumeAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/VFS.hpp>

#include <fstream>

namespace Desert::Assets
{
    CloudModellingVolumeAsset::CloudModellingVolumeAsset( AssetPriority           priority,
                                                          const Common::Filepath& filepath )
         : AssetBase( priority, filepath, AssetTypeID::CloudModellingVolume )
    {
        // The base class hands out a RANDOM uuid, which is right for an asset with no source of identity
        // and wrong for one that lives at a path. A fresh id every launch would mean the hero cloud
        // component's handle resolved to nothing after a restart, and the symptom would be "my cloud
        // disappeared when I reopened the editor" — a long way from the cause. Derived from the path,
        // exactly as CloudNoiseVolumeAsset and CloudTypeAsset do it, and in the CONSTRUCTOR so a registry
        // shell that has not loaded yet is already keyed by the handle the loaded asset will have.
        m_Metadata.Handle = Common::AssetHandle::FromCookedPath( m_Metadata.Filepath );
    }

    Common::BoolResultStr CloudModellingVolumeAsset::Load()
    {
        const std::string path = m_Metadata.Filepath.string();

        // Through the VFS rather than straight off the disk, so a packaged build reads the volume out of
        // its .dpak exactly like every other asset.
        const auto contents = Common::Utils::VFS::Exists( m_Metadata.Filepath )
                                   ? Common::Utils::VFS::ReadFile( m_Metadata.Filepath )
                                   : std::nullopt;

        std::vector<unsigned char> bytes;
        if ( contents.has_value() )
        {
            bytes.assign( contents->begin(), contents->end() );
        }
        else
        {
            std::ifstream file( m_Metadata.Filepath, std::ios::binary );
            if ( !file )
                return Common::MakeFormattedError<bool>( "Cloud modelling volume '{}' could not be opened", path );

            bytes.assign( std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() );
        }

        auto decoded = DecodeCloudModellingVolume( bytes );
        if ( !decoded )
        {
            m_Ready = false;
            return Common::MakeFormattedError<bool>( "Cloud modelling volume '{}' is not usable: {}", path,
                                                     decoded.GetError() );
        }

        m_Volume = decoded.ExtractValue();
        m_Ready  = true;
        ++m_Revision;

        LOG_INFO( "[Clouds] Modelling volume '{}' loaded: {}x{}x{} RGBA8 over {:.2f} x {:.2f} x {:.2f} km "
                  "({:.1f} m per voxel horizontally), {} lumps, blend {:.0f} m, generator v{}.",
                  path, kCloudModellingVolumeWidth, kCloudModellingVolumeHeight, kCloudModellingVolumeDepth,
                  m_Volume.Recipe.SizeKm.x, m_Volume.Recipe.SizeKm.y, m_Volume.Recipe.SizeKm.z,
                  m_Volume.Recipe.SizeKm.x * 1000.0f / static_cast<float>( kCloudModellingVolumeWidth ),
                  m_Volume.Recipe.Blobs.size(), m_Volume.Recipe.BlendRadiusKm * 1000.0f,
                  m_Volume.GeneratorVersion );

        // Said out loud rather than tolerated. A volume baked by an older generator still decodes and still
        // renders — the container did not change — but it is NOT what this build's maths produces, and an
        // artist comparing two clouds is entitled to know that one of them predates the sculpting itself.
        if ( m_Volume.GeneratorVersion != kCloudModellingGeneratorVersion )
            LOG_WARN( "[Clouds] Modelling volume '{}' was baked by generator v{}; this build generates v{}. Its "
                      "channels are whatever the older maths produced — re-bake it with Tools/CloudVolumeBaker "
                      "to compare like with like.",
                      path, m_Volume.GeneratorVersion, kCloudModellingGeneratorVersion );

        return BOOLSUCCESS;
    }

    Common::BoolResultStr CloudModellingVolumeAsset::Unload()
    {
        m_Volume.Voxels.clear();
        m_Volume.Voxels.shrink_to_fit();
        m_Ready = false;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr CloudModellingVolumeAsset::Save( const Common::Filepath&         filepath,
                                                           const CloudModellingVolumeData& volume )
    {
        if ( auto valid = ValidateCloudModellingRecipe( volume.Recipe ); !valid )
            return Common::MakeFormattedError<bool>( "refusing to write '{}': {}", filepath.string(),
                                                     valid.GetError() );

        if ( volume.Voxels.size() != kCloudModellingVoxelBytes )
            return Common::MakeFormattedError<bool>(
                 "refusing to write '{}': {} voxel bytes, and a {}x{}x{} RGBA8 volume is {}", filepath.string(),
                 volume.Voxels.size(), kCloudModellingVolumeWidth, kCloudModellingVolumeHeight,
                 kCloudModellingVolumeDepth, kCloudModellingVoxelBytes );

        std::error_code ec;
        if ( filepath.has_parent_path() )
            std::filesystem::create_directories( filepath.parent_path(), ec );

        const std::vector<unsigned char> encoded = EncodeCloudModellingVolume( volume );

        std::ofstream file( filepath, std::ios::binary | std::ios::trunc );
        if ( !file )
            return Common::MakeFormattedError<bool>( "'{}' could not be opened for writing", filepath.string() );

        file.write( reinterpret_cast<const char*>( encoded.data() ),
                    static_cast<std::streamsize>( encoded.size() ) );
        if ( !file )
            return Common::MakeFormattedError<bool>( "'{}' was opened but the {} bytes could not be written",
                                                     filepath.string(), encoded.size() );

        LOG_INFO( "[Clouds] Modelling volume written: '{}', {} lumps, {} bytes.", filepath.string(),
                  volume.Recipe.Blobs.size(), encoded.size() );
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets
