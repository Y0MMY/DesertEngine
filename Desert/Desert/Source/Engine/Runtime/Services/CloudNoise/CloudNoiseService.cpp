#include "CloudNoiseService.hpp"

#include <Engine/Core/Formats/ImageFormat.hpp>

#include <Common/Core/Logger.hpp>

namespace Desert::Runtime
{
    Common::BoolResultStr
    CloudNoiseService::Register( const std::shared_ptr<Assets::CloudNoiseVolumeAsset>& asset )
    {
        if ( !asset )
            return Common::MakeError( "CloudNoiseService::Register was given a null asset" );

        if ( !asset->IsReadyForUse() )
            return Common::MakeFormattedError<bool>( "cloud noise volume '{}' is not loaded",
                                                     asset->GetMetadata().Filepath.string() );

        const Assets::AssetHandle&          handle = asset->GetMetadata().Handle;
        const Assets::CloudNoiseVolumeData& data   = asset->GetVolume();

        if ( auto it = m_Volumes.find( handle );
             it != m_Volumes.end() && it->second.Revision == asset->GetRevision() )
            return BOOLSUCCESS;

        // A fresh image rather than an in-place update: the backend has no path for re-uploading a volume,
        // and the resolution itself may have changed. The previous image goes through Image3D's own
        // deletion queue when the last reference drops, which is what keeps a frame still reading it safe.
        const Core::Formats::Image3DSpecification spec{
             .Tag        = "CloudNoise:" + asset->GetMetadata().Filepath.filename().string(),
             .Width      = data.Params.Resolution,
             .Height     = data.Params.Resolution,
             .Depth      = data.Params.Resolution,
             .Format     = Core::Formats::ImageFormat::RGBA8F,
             .Data       = data.Voxels,
             .Properties = Core::Formats::Sample,
        };

        auto volume = Graphic::Image3D::Create( spec );
        if ( !volume )
            return Common::MakeFormattedError<bool>(
                 "the {0}x{0}x{0} RGBA8 volume for '{1}' could not be created on the device",
                 data.Params.Resolution, asset->GetMetadata().Filepath.string() );

        m_Volumes[handle] = Entry{ std::move( volume ), asset->GetRevision() };
        ++m_Generation;

        LOG_INFO( "[Clouds] Noise volume '{}' uploaded: {}^3 RGBA8, {:.2f} MiB on the device.",
                  asset->GetMetadata().Filepath.string(), data.Params.Resolution,
                  static_cast<double>( data.Voxels.size() ) / ( 1024.0 * 1024.0 ) );
        return BOOLSUCCESS;
    }

    void CloudNoiseService::SetDefault( const Assets::AssetHandle& handle )
    {
        m_Default = handle;
    }

    Graphic::Image3D* CloudNoiseService::Get( const Assets::AssetHandle& handle )
    {
        if ( handle != 0 )
        {
            if ( auto it = m_Volumes.find( handle ); it != m_Volumes.end() )
                return it->second.Volume.get();

            // NOT a silent fall-through. The artist chose a volume and it is not there; saying which
            // handle is the difference between a five-minute fix and an afternoon.
            LOG_ERROR( "[Clouds] Noise volume {} is referenced but not registered — falling back to the "
                       "default volume. The scene names a .dcnv the asset scan did not find.",
                       static_cast<uint64_t>( handle ) );
        }

        if ( m_Default != 0 )
        {
            if ( auto it = m_Volumes.find( m_Default ); it != m_Volumes.end() )
                return it->second.Volume.get();
        }

        if ( !m_ReportedMissingDefault )
        {
            m_ReportedMissingDefault = true;
            LOG_ERROR( "[Clouds] No default noise volume is registered, so the clouds cannot be shaped and "
                       "will not draw. Expected a .dcnv under the project's Assets/Clouds folder." );
        }
        return nullptr;
    }

    void CloudNoiseService::Clear()
    {
        m_Volumes.clear();
        m_Default = Assets::AssetHandle{ 0 };
        ++m_Generation;
    }
} // namespace Desert::Runtime
