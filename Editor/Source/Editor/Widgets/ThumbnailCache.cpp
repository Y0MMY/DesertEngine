#include "ThumbnailCache.hpp"

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>

#include <Engine/Core/Formats/ImageFormat.hpp>

#include <stb_image/stb_image.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace Desert::Editor
{
    // Bump whenever the thumbnail render path changes so all old thumbnails regenerate. v2: sky-IBL ambient
    // (old pre-IBL renders produced chrome/glass blobs that the source-modtime check never invalidated).
    // v3: output bumped 128 -> 256 px (128 looked low-res / "240p" when shown larger than 128 in the grid).
    // v4: PNG bumped to 1024 px (hi-res on disk, box-averaged down to kThumbMaxDim for the small grid display).
    // v5: studio-gradient backdrop in the preview scene (was the dull default sky).
    int ThumbnailCache::CacheVersion()
    {
        return 6; // v6: daylight sky backdrop + 180° mesh-facing fix
    }

    std::string ThumbnailCache::DiskPath( const std::string& assetPath )
    {
        std::string key = assetPath;
        for ( char& c : key )
            if ( !std::isalnum( static_cast<unsigned char>( c ) ) )
                c = '_';
        return ( Common::Constants::Path::COOKED_PATH / ( "Thumbnails/v" + std::to_string( CacheVersion() ) ) / ( key + ".png" ) ).string();
    }

    void ThumbnailCache::PurgeOldVersions()
    {
        std::error_code             ec;
        const std::filesystem::path root( Common::Constants::Path::COOKED_PATH / "Thumbnails" );
        if ( !std::filesystem::exists( root, ec ) )
            return;
        const std::string keep = "v" + std::to_string( CacheVersion() );
        for ( const auto& entry : std::filesystem::directory_iterator( root, ec ) )
        {
            if ( entry.is_directory( ec ) && entry.path().filename() == keep )
                continue;
            std::filesystem::remove_all( entry.path(), ec );
        }
    }

    std::shared_ptr<Graphic::Image2D> ThumbnailCache::Get( const std::string& sourcePath )
    {
        if ( const auto it = m_Cache.find( sourcePath ); it != m_Cache.end() )
            return it->second; // may be null (decode previously failed)

        if ( m_Cache.size() >= kMaxEntries )
            m_Cache.clear(); // simple bound; thumbnails re-decode lazily

        std::shared_ptr<Graphic::Image2D> result;

        int      w = 0, h = 0, ch = 0;
        stbi_uc* pixels = stbi_load( sourcePath.c_str(), &w, &h, &ch, 4 );
        if ( pixels && w > 0 && h > 0 )
        {
            // Box-average downscale to <= kThumbMaxDim. A large (1024) source PNG shown tiny needs averaging,
            // not nearest-neighbour (which would alias / shimmer); this is effectively extra supersampling.
            const int maxSide = std::max( w, h );
            const int tw      = maxSide > kThumbMaxDim ? std::max( 1, w * kThumbMaxDim / maxSide ) : w;
            const int th      = maxSide > kThumbMaxDim ? std::max( 1, h * kThumbMaxDim / maxSide ) : h;

            std::vector<unsigned char> dst( static_cast<size_t>( tw ) * th * 4 );
            for ( int y = 0; y < th; ++y )
            {
                const int sy0 = y * h / th;
                const int sy1 = std::max( sy0 + 1, ( y + 1 ) * h / th );
                for ( int x = 0; x < tw; ++x )
                {
                    const int sx0 = x * w / tw;
                    const int sx1 = std::max( sx0 + 1, ( x + 1 ) * w / tw );

                    uint32_t acc[4] = { 0, 0, 0, 0 };
                    uint32_t n      = 0;
                    for ( int yy = sy0; yy < sy1; ++yy )
                        for ( int xx = sx0; xx < sx1; ++xx )
                        {
                            const unsigned char* s = pixels + ( static_cast<size_t>( yy ) * w + xx ) * 4;
                            acc[0] += s[0]; acc[1] += s[1]; acc[2] += s[2]; acc[3] += s[3];
                            ++n;
                        }
                    const uint32_t   div = std::max( 1u, n );
                    unsigned char*   d   = dst.data() + ( static_cast<size_t>( y ) * tw + x ) * 4;
                    d[0] = static_cast<unsigned char>( acc[0] / div );
                    d[1] = static_cast<unsigned char>( acc[1] / div );
                    d[2] = static_cast<unsigned char>( acc[2] / div );
                    d[3] = static_cast<unsigned char>( acc[3] / div );
                }
            }

            Core::Formats::Image2DSpecification spec = {
                 .Tag        = "Thumb_" + std::filesystem::path( sourcePath ).filename().string(),
                 .Width      = static_cast<uint32_t>( tw ),
                 .Height     = static_cast<uint32_t>( th ),
                 .Format     = Core::Formats::ImageFormat::RGBA8F,
                 .Mips       = 1u,
                 .Data       = std::move( dst ),
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::Sample,
            };
            result = Graphic::Image2D::Create( spec, nullptr );
        }
        if ( pixels )
            stbi_image_free( pixels );

        m_Cache[sourcePath] = result; // cache success or failure (null)
        return result;
    }

    void ThumbnailCache::Invalidate( const std::string& sourcePath )
    {
        m_Cache.erase( sourcePath );
    }

    void ThumbnailCache::Clear()
    {
        m_Cache.clear();
    }

    std::unordered_set<ThumbnailCache*>& ThumbnailCache::Live()
    {
        // Function-local so it is constructed before the first cache registers, whatever the translation
        // unit order is — three of the owners are themselves function-statics in other files.
        static std::unordered_set<ThumbnailCache*> s_Live;
        return s_Live;
    }

    ThumbnailCache::ThumbnailCache()
    {
        Live().insert( this );
    }

    ThumbnailCache::~ThumbnailCache()
    {
        Live().erase( this );
    }

    void ThumbnailCache::ReleaseAll()
    {
        // See the header. Clear(), not destroy: these caches outlive this call by design — the three that
        // matter are function-statics that will not be destroyed until the process ends — and what has to
        // go is the GPU image each one holds, not the map that held it.
        std::size_t images = 0;
        for ( ThumbnailCache* cache : Live() )
        {
            images += cache->m_Cache.size();
            cache->Clear();
        }

        LOG_INFO( "[Thumbnails] released {} cached image(s) from {} cache(s) on shutdown.", images,
                  Live().size() );
    }
} // namespace Desert::Editor
