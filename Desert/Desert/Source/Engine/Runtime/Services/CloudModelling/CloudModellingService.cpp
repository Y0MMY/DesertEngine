#include "CloudModellingService.hpp"

#include <Engine/Core/Formats/ImageFormat.hpp>

#include <Common/Core/Logger.hpp>

namespace Desert::Runtime
{
    Common::BoolResultStr
    CloudModellingService::Register( const std::shared_ptr<Assets::CloudModellingVolumeAsset>& asset )
    {
        if ( !asset )
            return Common::MakeError( "CloudModellingService::Register was given a null asset" );

        if ( !asset->IsReadyForUse() )
            return Common::MakeFormattedError<bool>( "cloud modelling volume '{}' is not loaded",
                                                     asset->GetMetadata().Filepath.string() );

        const Assets::AssetHandle&              handle = asset->GetMetadata().Handle;
        const Assets::CloudModellingVolumeData& data   = asset->GetVolume();

        if ( auto it = m_Volumes.find( handle );
             it != m_Volumes.end() && it->second.Revision == asset->GetRevision() )
            return BOOLSUCCESS;

        // A fresh image rather than an in-place update: the backend has no path for re-uploading a volume.
        // The previous image goes through Image3D's own deletion queue when the last reference drops,
        // which is what keeps a frame still reading it safe.
        const Core::Formats::Image3DSpecification spec{
             .Tag        = "CloudBody:" + asset->GetMetadata().Filepath.filename().string(),
             .Width      = Assets::kCloudModellingVolumeWidth,
             .Height     = Assets::kCloudModellingVolumeHeight,
             .Depth      = Assets::kCloudModellingVolumeDepth,
             .Format     = Core::Formats::ImageFormat::RGBA8F,
             .Data       = data.Voxels,
             .Properties = Core::Formats::Sample,
        };

        auto volume = Graphic::Image3D::Create( spec );
        if ( !volume )
            return Common::MakeFormattedError<bool>(
                 "the {}x{}x{} RGBA8 volume for '{}' could not be created on the device",
                 Assets::kCloudModellingVolumeWidth, Assets::kCloudModellingVolumeHeight,
                 Assets::kCloudModellingVolumeDepth, asset->GetMetadata().Filepath.string() );

        m_Volumes[handle] = Entry{ std::move( volume ), data.Recipe.SizeKm, asset->GetRevision() };
        ++m_Generation;

        LOG_INFO( "[Clouds] Modelling volume '{}' uploaded: {}x{}x{} RGBA8, {:.2f} MiB on the device, "
                  "{:.2f} x {:.2f} x {:.2f} km as sculpted.",
                  asset->GetMetadata().Filepath.string(), Assets::kCloudModellingVolumeWidth,
                  Assets::kCloudModellingVolumeHeight, Assets::kCloudModellingVolumeDepth,
                  static_cast<double>( data.Voxels.size() ) / ( 1024.0 * 1024.0 ), data.Recipe.SizeKm.x,
                  data.Recipe.SizeKm.y, data.Recipe.SizeKm.z );
        return BOOLSUCCESS;
    }

    Graphic::Image3D* CloudModellingService::Get( const Assets::AssetHandle& handle )
    {
        // AN EMPTY SLOT IS SILENCE, not an error and not a default. There is no built-in hero cloud and
        // there must not be one: a body nobody sculpted appearing in a scene is a cloud the artist cannot
        // explain, where an empty noise slot resolving to the shipped default is a sky they expect.
        if ( handle == 0 )
            return nullptr;

        if ( auto it = m_Volumes.find( handle ); it != m_Volumes.end() )
            return it->second.Volume.get();

        // NOT a silent fall-through. The artist chose a body and it is not there; saying which handle is
        // the difference between a five-minute fix and an afternoon.
        LOG_ERROR( "[Clouds] Cloud modelling volume {} is referenced by a hero cloud but not registered, so "
                   "that cloud will not draw. The scene names a .dcmv the asset scan did not find.",
                   static_cast<uint64_t>( handle ) );
        return nullptr;
    }

    glm::vec3 CloudModellingService::GetSizeKm( const Assets::AssetHandle& handle )
    {
        if ( auto it = m_Volumes.find( handle ); it != m_Volumes.end() )
            return it->second.SizeKm;
        return glm::vec3( 0.0f );
    }

    void CloudModellingService::Clear()
    {
        m_Volumes.clear();
        ++m_Generation;
    }
} // namespace Desert::Runtime
