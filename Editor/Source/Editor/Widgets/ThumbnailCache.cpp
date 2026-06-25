#include "ThumbnailCache.hpp"

#include <Engine/Core/Formats/ImageFormat.hpp>

#include <stb_image/stb_image.h>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace Desert::Editor
{
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
            // Nearest-neighbour downscale to <= kThumbMaxDim (bounds VRAM; quality is fine for a thumbnail).
            const int maxSide = std::max( w, h );
            const int tw      = maxSide > kThumbMaxDim ? std::max( 1, w * kThumbMaxDim / maxSide ) : w;
            const int th      = maxSide > kThumbMaxDim ? std::max( 1, h * kThumbMaxDim / maxSide ) : h;

            std::vector<unsigned char> dst( static_cast<size_t>( tw ) * th * 4 );
            for ( int y = 0; y < th; ++y )
            {
                const int sy = y * h / th;
                for ( int x = 0; x < tw; ++x )
                {
                    const int sx  = x * w / tw;
                    const unsigned char* s = pixels + ( static_cast<size_t>( sy ) * w + sx ) * 4;
                    unsigned char*       d = dst.data() + ( static_cast<size_t>( y ) * tw + x ) * 4;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
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

    void ThumbnailCache::Clear()
    {
        m_Cache.clear();
    }
} // namespace Desert::Editor
