#include <Engine/Assets/CloudNoiseVolumeAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/VFS.hpp>

#include <fstream>

namespace Desert::Assets
{
    CloudNoiseVolumeAsset::CloudNoiseVolumeAsset( AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, AssetTypeID::CloudNoiseVolume )
    {
        // The base class hands out a RANDOM uuid, which is right for an asset with no source of identity
        // and wrong for one that lives at a path. A fresh id every launch means any field that stored a
        // HANDLE to a volume would resolve to nothing after a restart, and the symptom would be "the
        // clouds changed when I reopened the editor" — a long way from the cause. Derived from the path,
        // exactly as StaticMeshAsset and CloudTypeAsset do it, and in the CONSTRUCTOR so a registry shell
        // that has not loaded yet is already keyed by the handle the loaded asset will have.
        //
        // Nothing references a volume by handle today (a .decloudtype names it by relative path), so this
        // fixes no visible defect. It removes the mine: the first reflected field to take a .dcnv would
        // otherwise have inherited the broken reference for free.
        m_Metadata.Handle = Common::AssetHandle::FromCookedPath( m_Metadata.Filepath );
    }

    Common::BoolResultStr CloudNoiseVolumeAsset::Load()
    {
        const std::string path = m_Metadata.Filepath.string();

        // Through the VFS rather than straight off the disk, so a packaged build reads the volume out of
        // its .dpak exactly like every other asset. `ReadFile` returns the bytes as a std::string; a
        // container is binary, and std::string is byte-transparent, so the conversion is a copy and not an
        // interpretation.
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
                return Common::MakeFormattedError<bool>( "Cloud noise volume '{}' could not be opened", path );

            bytes.assign( std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() );
        }

        auto decoded = DecodeCloudNoiseVolume( bytes );
        if ( !decoded )
        {
            m_Ready = false;
            return Common::MakeFormattedError<bool>( "Cloud noise volume '{}' is not usable: {}", path,
                                                     decoded.GetError() );
        }

        m_Volume = decoded.ExtractValue();
        m_Ready  = true;
        ++m_Revision;

        LOG_INFO( "[Clouds] Noise volume '{}' loaded: {}^3 RGBA8, seed {}, generator v{}, periods "
                  "{:.0f}/{:.0f} wispy and {:.0f}/{:.0f} billowy, curl {:.2f}.",
                  path, m_Volume.Params.Resolution, m_Volume.Params.Seed, m_Volume.GeneratorVersion,
                  m_Volume.Params.WispyPeriodLowFrequency, m_Volume.Params.WispyPeriodHighFrequency,
                  m_Volume.Params.BillowPeriodLowFrequency, m_Volume.Params.BillowPeriodHighFrequency,
                  m_Volume.Params.CurlStrength );

        // Said out loud rather than tolerated. A volume baked by an older generator still decodes and still
        // renders — the container did not change — but it is NOT what this build's maths produces, and an
        // artist comparing two volumes is entitled to know that one of them predates the noise itself.
        if ( m_Volume.GeneratorVersion != kCloudNoiseGeneratorVersion )
            LOG_WARN( "[Clouds] Noise volume '{}' was baked by generator v{}; this build generates v{}. Its "
                      "channels are whatever the older maths produced — re-bake it to compare like with like.",
                      path, m_Volume.GeneratorVersion, kCloudNoiseGeneratorVersion );

        return BOOLSUCCESS;
    }

    Common::BoolResultStr CloudNoiseVolumeAsset::Unload()
    {
        m_Volume.Voxels.clear();
        m_Volume.Voxels.shrink_to_fit();
        m_Ready = false;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr CloudNoiseVolumeAsset::Save( const Common::Filepath&     filepath,
                                                       const CloudNoiseVolumeData& volume )
    {
        if ( auto valid = ValidateCloudNoiseVolumeParams( volume.Params ); !valid )
            return Common::MakeFormattedError<bool>( "refusing to write '{}': {}", filepath.string(),
                                                     valid.GetError() );

        const uint64_t expected = volume.VoxelCount() * 4u;
        if ( volume.Voxels.size() != expected )
            return Common::MakeFormattedError<bool>(
                 "refusing to write '{}': {} voxel bytes for a {}^3 volume, which needs {}", filepath.string(),
                 volume.Voxels.size(), volume.Params.Resolution, expected );

        std::error_code ec;
        if ( filepath.has_parent_path() )
            std::filesystem::create_directories( filepath.parent_path(), ec );

        const std::vector<unsigned char> encoded = EncodeCloudNoiseVolume( volume );

        std::ofstream file( filepath, std::ios::binary | std::ios::trunc );
        if ( !file )
            return Common::MakeFormattedError<bool>( "'{}' could not be opened for writing", filepath.string() );

        file.write( reinterpret_cast<const char*>( encoded.data() ),
                    static_cast<std::streamsize>( encoded.size() ) );
        if ( !file )
            return Common::MakeFormattedError<bool>( "'{}' was opened but the {} bytes could not be written",
                                                     filepath.string(), encoded.size() );

        LOG_INFO( "[Clouds] Noise volume written: '{}', {}^3 RGBA8, {} bytes, seed {}.", filepath.string(),
                  volume.Params.Resolution, encoded.size(), volume.Params.Seed );
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets
