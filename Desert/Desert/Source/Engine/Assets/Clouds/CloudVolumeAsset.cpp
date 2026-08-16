#include <Engine/Assets/Clouds/CloudVolumeAsset.hpp>

#include <Common/Core/Logger.hpp>

#include <fstream>
#include <vector>

namespace Desert::Assets
{
    CloudVolumeAsset::CloudVolumeAsset( AssetPriority priority, const Common::Filepath& filepath )
         : AssetBase( priority, filepath, AssetTypeID::CloudVolume )
    {
    }

    Common::BoolResultStr CloudVolumeAsset::Load()
    {
        const std::string path = m_Metadata.Filepath.string();

        std::ifstream stream( m_Metadata.Filepath, std::ios::binary | std::ios::ate );
        if ( !stream )
        {
            LOG_ERROR( "[CloudVolume] Cannot open {}", path );
            return Common::MakeFormattedError<bool>( "Cannot open the cloud volume {}", path );
        }

        const std::streamoff size = stream.tellg();
        stream.seekg( 0, std::ios::beg );

        std::vector<unsigned char> raw( static_cast<size_t>( size ) );
        if ( !stream.read( reinterpret_cast<char*>( raw.data() ), size ) )
        {
            LOG_ERROR( "[CloudVolume] Cannot read {} ({} bytes expected)", path, size );
            return Common::MakeFormattedError<bool>( "Cannot read the cloud volume {}", path );
        }

        auto parsed = Graphic::ReadCloudVolume( raw.data(), raw.size() );
        if ( !parsed.IsSuccess() )
        {
            // The reason carries the actual numbers (dimensions, byte counts, the version it found), so
            // the log line is enough to fix the file without opening it in a hex editor.
            LOG_ERROR( "[CloudVolume] {}: {}", path, parsed.GetError() );
            return Common::MakeError<bool>( parsed.GetError() );
        }

        m_Volume      = parsed.GetValue();
        m_ReadyForUse = true;

        const auto& header = m_Volume.Header;
        LOG_INFO( "[CloudVolume] Loaded {} — {}x{}x{} RGBA8 ({:.2f} MiB) covering {:.0f} x {:.0f} x {:.0f} m "
                  "at {:.1f} m per voxel.",
                  path, header.Width, header.Height, header.Depth,
                  static_cast<double>( m_Volume.Voxels.size() ) / ( 1024.0 * 1024.0 ), header.ExtentX / 100.0f,
                  header.ExtentY / 100.0f, header.ExtentZ / 100.0f,
                  header.ExtentX / 100.0f / static_cast<float>( header.Width ) );

        return Common::MakeSuccess( true );
    }

    Common::BoolResultStr CloudVolumeAsset::Unload()
    {
        m_Volume      = Graphic::CloudVolume{};
        m_ReadyForUse = false;
        return Common::MakeSuccess( true );
    }
} // namespace Desert::Assets
