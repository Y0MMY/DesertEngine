#include "CloudModellingService.hpp"

#include <Engine/Core/Formats/ImageFormat.hpp>
#include <Engine/Graphic/Clouds/CloudAuthoredPayload.hpp>

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

        if ( data.Voxels.size() != static_cast<size_t>( Assets::kCloudModellingVoxelBytes ) )
            return Common::MakeFormattedError<bool>( "'{}' decoded to {} bytes where a modelling volume is {}",
                                                     asset->GetMetadata().Filepath.string(), data.Voxels.size(),
                                                     Assets::kCloudModellingVoxelBytes );

        if ( auto it = m_Volumes.find( handle );
             it != m_Volumes.end() && it->second.Revision == asset->GetRevision() )
            return BOOLSUCCESS;

        m_Volumes[handle] = Entry{ asset, data.Recipe.SizeKm, asset->GetRevision() };

        LOG_INFO( "[Clouds] Modelling volume '{}' registered: {}x{}x{} RGBA8, {:.2f} MiB, "
                  "{:.2f} x {:.2f} x {:.2f} km as sculpted.",
                  asset->GetMetadata().Filepath.string(), Assets::kCloudModellingVolumeWidth,
                  Assets::kCloudModellingVolumeHeight, Assets::kCloudModellingVolumeDepth,
                  static_cast<double>( data.Voxels.size() ) / ( 1024.0 * 1024.0 ), data.Recipe.SizeKm.x,
                  data.Recipe.SizeKm.y, data.Recipe.SizeKm.z );
        return BOOLSUCCESS;
    }

    bool CloudModellingService::HasBody( const Assets::AssetHandle& handle )
    {
        // AN EMPTY SLOT IS SILENCE, not an error and not a default. There is no built-in hero cloud and
        // there must not be one: a body nobody sculpted appearing in a scene is a cloud the artist cannot
        // explain, where an empty noise slot resolving to the shipped default is a sky they expect.
        if ( handle == 0 )
            return false;

        if ( m_Volumes.find( handle ) != m_Volumes.end() )
            return true;

        // NOT a silent fall-through. The artist chose a body and it is not there; saying which handle is
        // the difference between a five-minute fix and an afternoon.
        LOG_ERROR( "[Clouds] Cloud modelling volume {} is referenced by a hero cloud but not registered, so "
                   "that cloud will not draw. The scene names a .dcmv the asset scan did not find.",
                   static_cast<uint64_t>( handle ) );
        return false;
    }

    glm::vec3 CloudModellingService::GetSizeKm( const Assets::AssetHandle& handle )
    {
        if ( auto it = m_Volumes.find( handle ); it != m_Volumes.end() )
            return it->second.SizeKm;
        return glm::vec3( 0.0f );
    }

    CloudModellingAtlasBinding CloudModellingService::EnsureAtlas( const std::vector<Assets::AssetHandle>& bodies )
    {
        if ( bodies.empty() )
        {
            // NOT a rebuild to nothing: the previous atlas is released, because a scene that lost its hero
            // clouds must give its megabytes back. The caller binds the fallback image.
            if ( m_Atlas )
            {
                m_Atlas.reset();
                m_AtlasSlabs.clear();
                m_AtlasRevisions.clear();
                ++m_Generation;
            }
            return {};
        }

        // Is the live atlas already the one being asked for? Slab order is part of the question: the same
        // bodies in a different order are a different atlas, because a slab index is baked into every
        // instance the renderer packs.
        std::vector<uint32_t> revisions;
        revisions.reserve( bodies.size() );
        for ( const Assets::AssetHandle& handle : bodies )
        {
            const auto it = m_Volumes.find( handle );
            if ( it == m_Volumes.end() )
            {
                LOG_ERROR( "[Clouds] The atlas was asked for volume {}, which is not registered. No hero "
                           "cloud will draw this frame.",
                           static_cast<uint64_t>( handle ) );
                return {};
            }
            revisions.push_back( it->second.Revision );
        }

        if ( m_Atlas && m_AtlasSlabs == bodies && m_AtlasRevisions == revisions )
            return CloudModellingAtlasBinding{ m_Atlas.get(), static_cast<uint32_t>( m_AtlasSlabs.size() ) };

        std::vector<const std::vector<unsigned char>*> voxels;
        voxels.reserve( bodies.size() );
        for ( const Assets::AssetHandle& handle : bodies )
            voxels.push_back( &m_Volumes[handle].Asset->GetVolume().Voxels );

        const auto assembled = Assets::AssembleCloudModellingAtlas( voxels );
        if ( !assembled )
        {
            LOG_ERROR( "[Clouds] The modelling atlas could not be assembled: {}", assembled.GetError() );
            return {};
        }

        const std::vector<unsigned char>& bytes = assembled.GetValue();

        // A FRESH IMAGE rather than an in-place update: the backend has no path for re-uploading a volume.
        // The previous image goes through Image3D's own deletion queue when the last reference drops,
        // which is what keeps a frame still reading it safe.
        const Core::Formats::Image3DSpecification spec{
             .Tag        = "CloudBodyAtlas:" + std::to_string( bodies.size() ),
             .Width      = Assets::kCloudModellingVolumeWidth,
             .Height     = Assets::kCloudModellingVolumeHeight,
             .Depth      = Assets::kCloudModellingVolumeDepth * static_cast<uint32_t>( bodies.size() ),
             .Format     = Core::Formats::ImageFormat::RGBA8F,
             .Data       = bytes,
             .Properties = Core::Formats::Sample,
        };

        auto atlas = Graphic::Image3D::Create( spec );
        if ( !atlas )
        {
            LOG_ERROR( "[Clouds] The {}x{}x{} RGBA8 modelling atlas ({} bodies) could not be created on the "
                       "device, so no hero cloud will draw.",
                       spec.Width, spec.Height, spec.Depth, bodies.size() );
            return {};
        }

        m_Atlas          = std::move( atlas );
        m_AtlasSlabs     = bodies;
        m_AtlasRevisions = std::move( revisions );
        ++m_Generation;

        LOG_INFO( "[Clouds] Modelling atlas built: {} bodies, {}x{}x{} RGBA8, {:.2f} MiB on the device.",
                  bodies.size(), spec.Width, spec.Height, spec.Depth,
                  static_cast<double>( bytes.size() ) / ( 1024.0 * 1024.0 ) );

        return CloudModellingAtlasBinding{ m_Atlas.get(), static_cast<uint32_t>( m_AtlasSlabs.size() ) };
    }

    void CloudModellingService::Clear()
    {
        m_Volumes.clear();
        m_Atlas.reset();
        m_AtlasSlabs.clear();
        m_AtlasRevisions.clear();
        ++m_Generation;
    }
} // namespace Desert::Runtime
