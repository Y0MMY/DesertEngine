#include "CloudVolumeAtlas.hpp"

#include <Engine/Graphic/Renderer.hpp>

#include <Common/Core/Logger.hpp>

#include <algorithm>
#include <string>

namespace Desert::Graphic
{
    namespace
    {
        double BytesToMiB( uint64_t bytes )
        {
            return static_cast<double>( bytes ) / ( 1024.0 * 1024.0 );
        }
    } // namespace

    CloudVolumeAtlas::CloudVolumeAtlas( const CloudVolumeAtlasLayout& layout ) : m_Layout( layout )
    {
        m_Tiles.resize( CloudVolumeAtlasTileCount( m_Layout ) );

        // Zero-filled, and that zero is load-bearing: a free tile reads as a profile of 0 everywhere, so
        // an instance record left pointing at one contributes no density rather than whatever the last
        // cloud to live there looked like.
        m_Pixels.assign( static_cast<size_t>( CloudVolumeAtlasBytes( m_Layout ) ), 0 );
    }

    size_t CloudVolumeAtlas::TilesInUse() const
    {
        return static_cast<size_t>(
             std::count_if( m_Tiles.begin(), m_Tiles.end(), []( const Tile& t ) { return t.LeaseCount > 0; } ) );
    }

    std::optional<uint32_t> CloudVolumeAtlas::Find( uint64_t key ) const
    {
        if ( key == 0 )
            return std::nullopt;

        for ( size_t i = 0; i < m_Tiles.size(); ++i )
        {
            if ( m_Tiles[i].LeaseCount > 0 && m_Tiles[i].Key == key )
                return static_cast<uint32_t>( i );
        }
        return std::nullopt;
    }

    Common::ResultStr<uint32_t> CloudVolumeAtlas::Acquire( uint64_t key, const CloudVolume& volume )
    {
        if ( key == 0 )
            return Common::MakeError<uint32_t>(
                 "A cloud volume atlas tile cannot be keyed on 0 — that is the handle of an empty asset slot" );

        // Already resident: another entity references the same .dvol, which is exactly the re-use the
        // instancing model is for. No upload, no image rebuild.
        if ( const auto existing = Find( key ) )
        {
            ++m_Tiles[*existing].LeaseCount;
            return Common::MakeSuccess( *existing );
        }

        const auto free =
             std::find_if( m_Tiles.begin(), m_Tiles.end(), []( const Tile& t ) { return t.LeaseCount == 0; } );
        if ( free == m_Tiles.end() )
            return Common::MakeFormattedError<uint32_t>(
                 "All {} cloud volume atlas tiles are in use. Remove a Cloud Volume from the scene, or "
                 "point several of them at the same .dvol — identical volumes share one tile.",
                 m_Tiles.size() );

        const uint32_t tileIndex = static_cast<uint32_t>( std::distance( m_Tiles.begin(), free ) );

        const auto written = CloudVolumeAtlasWriteTile( m_Layout, tileIndex, volume, m_Pixels );
        if ( !written.IsSuccess() )
            return Common::MakeError<uint32_t>( written.GetError() );

        free->Key        = key;
        free->LeaseCount = 1;

        // The resident set changed, so a previously latched failure describes a different atlas than the
        // one about to be built. Give it the retry it has earned.
        m_Failed = false;

        const auto uploaded = Upload();
        if ( !uploaded.IsSuccess() )
        {
            free->Key        = 0;
            free->LeaseCount = 0;
            return Common::MakeError<uint32_t>( uploaded.GetError() );
        }

        return Common::MakeSuccess( tileIndex );
    }

    void CloudVolumeAtlas::Release( uint64_t key )
    {
        const auto tile = Find( key );
        if ( !tile )
        {
            // Not silently ignored: an unmatched Release means the caller's bookkeeping and this class's
            // disagree, and the symptom of that is a tile that is never freed.
            LOG_WARN( "[CloudVolumes] Release of asset handle {} which holds no atlas tile.", key );
            return;
        }

        Tile& entry = m_Tiles[*tile];
        --entry.LeaseCount;
        if ( entry.LeaseCount > 0 )
            return;

        entry.Key = 0;

        // Blank the tile so the next Acquire cannot see the previous cloud through a stale instance
        // record, and so the atlas image on the GPU matches what this class believes it holds.
        const glm::uvec3 origin   = CloudVolumeAtlasTileOrigin( m_Layout, *tile );
        const glm::uvec3 dims     = CloudVolumeAtlasDimensions( m_Layout );
        const size_t     rowBytes = static_cast<size_t>( m_Layout.TileWidth ) * kCloudVolumeChannels;
        for ( uint32_t z = 0; z < m_Layout.TileDepth; ++z )
        {
            for ( uint32_t y = 0; y < m_Layout.TileHeight; ++y )
            {
                const size_t at =
                     ( ( static_cast<size_t>( origin.z + z ) * dims.y + ( origin.y + y ) ) * dims.x + origin.x ) *
                     kCloudVolumeChannels;
                std::fill_n( m_Pixels.begin() + static_cast<std::ptrdiff_t>( at ), rowBytes, 0 );
            }
        }

        m_Failed = false;

        if ( TilesInUse() == 0 )
        {
            // Nothing wants the atlas any more: give the 32 MiB back rather than hold it for a scene
            // that may never place another hero cloud.
            Renderer::GetInstance().WaitDeviceIdle();
            m_Image.reset();
            LOG_INFO( "[CloudVolumes] Released the hero-cloud atlas — {:.2f} MiB, no scene is using it any "
                      "more.",
                      BytesToMiB( CloudVolumeAtlasBytes( m_Layout ) ) );
            return;
        }

        const auto uploaded = Upload();
        if ( !uploaded.IsSuccess() )
            LOG_ERROR( "[CloudVolumes] {}", uploaded.GetError() );
    }

    Common::BoolResultStr CloudVolumeAtlas::Upload()
    {
        if ( m_Failed )
            return Common::MakeError<bool>(
                 "The hero-cloud atlas failed to allocate and will not be retried until its tile set changes" );

        const glm::uvec3 dims = CloudVolumeAtlasDimensions( m_Layout );

        // The image is replaced wholesale, and a frame in flight may still hold a descriptor pointing at
        // the old one — the same reason the noise set and the sky environment bake idle the device here.
        if ( m_Image )
            Renderer::GetInstance().WaitDeviceIdle();

        const Core::Formats::Image3DSpecification spec{
             .Tag        = "CloudVolumeAtlas",
             .Width      = dims.x,
             .Height     = dims.y,
             .Depth      = dims.z,
             .Format     = Core::Formats::ImageFormat::RGBA8F,
             .Data       = m_Pixels,
             .Properties = Core::Formats::Sample,
        };

        Image3DRef image = Image3D::Create( spec );
        if ( !image )
        {
            m_Failed = true;
            return Common::MakeFormattedError<bool>(
                 "Could not allocate the {}x{}x{} RGBA8 hero-cloud atlas ({:.2f} MiB). Hero clouds will not "
                 "render.",
                 dims.x, dims.y, dims.z, BytesToMiB( CloudVolumeAtlasBytes( m_Layout ) ) );
        }

        m_Image = std::move( image );

        LOG_INFO( "[CloudVolumes] Atlas {}x{}x{} RGBA8 ({:.2f} MiB) for {} tiles of {}x{}x{}; {} in use, {} "
                  "free.",
                  dims.x, dims.y, dims.z, BytesToMiB( CloudVolumeAtlasBytes( m_Layout ) ), m_Tiles.size(),
                  m_Layout.TileWidth, m_Layout.TileHeight, m_Layout.TileDepth, TilesInUse(),
                  m_Tiles.size() - TilesInUse() );

        return Common::MakeSuccess( true );
    }
} // namespace Desert::Graphic
