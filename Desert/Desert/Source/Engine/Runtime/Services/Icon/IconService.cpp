#include "IconService.hpp"

#include <Engine/Vector/VectorImage.hpp>

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <algorithm>
#include <filesystem>

namespace Desert::Runtime
{
    namespace
    {
        // Bake resolution of the inner box, plus the gutter the distance field spreads over. 64 is the
        // same ballpark as the font atlas (48) — an icon is drawn far larger than a glyph, and the SDF
        // reconstructs the edge analytically, so this is about gradient quality, not pixel resolution.
        constexpr uint32_t kIconSize    = 64;
        constexpr int      kIconPadding = 6;
    } // namespace

    uint64_t IconService::RegisterIcon( const std::string& svgPath )
    {
        if ( svgPath.empty() )
            return 0;
        const uint64_t handle = static_cast<uint64_t>( Common::AssetHandle::FromKey( svgPath ) );
        if ( m_HandleToPath.emplace( handle, svgPath ).second )
            m_Available.push_back( svgPath ); // first sighting -> offer it in the picker
        return handle;
    }

    std::string IconService::PathForHandle( uint64_t handle )
    {
        if ( handle == 0 )
            return "";
        if ( const auto it = m_HandleToPath.find( handle ); it != m_HandleToPath.end() )
            return it->second;
        // A saved scene may reference an icon we haven't scanned yet — fill the registry and retry; the
        // deterministic handle matches as long as the .svg is discoverable.
        EnsurePreloaded();
        const auto it = m_HandleToPath.find( handle );
        return it == m_HandleToPath.end() ? "" : it->second;
    }

    Icon* IconService::Get( uint64_t handle )
    {
        const std::string path = PathForHandle( handle );
        if ( path.empty() )
            return nullptr;
        if ( const auto it = m_Icons.find( path ); it != m_Icons.end() )
            return it->second.get();

        const auto svg = Common::Utils::FileSystem::ReadByteFileContent( path );
        if ( svg.empty() )
        {
            LOG_ERROR( "[IconService] Cannot read icon '{}'", path );
            m_Icons[path] = std::make_unique<Icon>(); // negative-cache: don't retry every frame
            return m_Icons[path].get();
        }

        const Vector::VectorImage image =
             Vector::ParseSvg( reinterpret_cast<const char*>( svg.data() ), svg.size() );
        const std::vector<uint8_t> sdf  = Vector::RasterizeSdf( image, kIconSize, kIconPadding );
        auto                       icon = std::make_unique<Icon>();
        if ( sdf.empty() )
        {
            LOG_ERROR( "[IconService] '{}' has no filled shapes this importer understands", path );
            m_Icons[path] = std::move( icon );
            return m_Icons[path].get();
        }

        const uint32_t dim = kIconSize + 2u * static_cast<uint32_t>( kIconPadding );

        // The engine's sampled formats are RGBA8 (no R8), so RGB carries the distance field — the UI text
        // shader samples .r and reconstructs the edge itself. ALPHA is free (the shader never reads it), so
        // it gets a sharpened coverage mask instead: any plain alpha-blended draw — the editor's Details
        // preview — then shows the icon's real silhouette rather than a soft grey blob, at zero cost.
        std::vector<unsigned char> rgba( static_cast<size_t>( dim ) * dim * 4 );
        const float                edge     = static_cast<float>( Vector::kSdfOnEdgeValue );
        const float                perTexel = edge / static_cast<float>( kIconPadding ); // SDF units / texel
        for ( size_t i = 0; i < sdf.size(); ++i )
        {
            const float cov = ( static_cast<float>( sdf[i] ) - edge ) * ( 255.0f / perTexel ) + 128.0f;
            rgba[i * 4 + 0] = sdf[i];
            rgba[i * 4 + 1] = sdf[i];
            rgba[i * 4 + 2] = sdf[i];
            rgba[i * 4 + 3] = static_cast<unsigned char>( std::clamp( cov, 0.0f, 255.0f ) );
        }

        Core::Formats::Image2DSpecification spec = { .Tag        = "IconSDF:" + path,
                                                     .Width      = dim,
                                                     .Height     = dim,
                                                     .Format     = Core::Formats::ImageFormat::RGBA8F,
                                                     .Mips       = 1,
                                                     .Data       = std::move( rgba ),
                                                     .Usage      = Core::Formats::Image2DUsage::Image2D,
                                                     .Properties = Core::Formats::Sample };

        icon->Atlas = Graphic::Image2D::Create( spec, nullptr );
        if ( !icon->Atlas )
        {
            LOG_ERROR( "[IconService] GPU upload failed for '{}'", path );
        }
        else
        {
            LOG_INFO( "[IconService] Imported '{}' -> {}x{} SDF", path, dim, dim );
        }

        icon->Aspect  = image.Height > 0.0f ? image.Width / image.Height : 1.0f;
        m_Icons[path] = std::move( icon );
        return m_Icons[path].get();
    }

    const std::vector<std::string>& IconService::AvailableIcons()
    {
        EnsurePreloaded();
        return m_Available;
    }

    void IconService::Clear()
    {
        m_Icons.clear();
        m_HandleToPath.clear();
        m_Available.clear();
        m_Scanned = false;
    }

    void IconService::EnsurePreloaded()
    {
        if ( m_Scanned )
            return;
        m_Scanned = true;

        // This project's Assets tree (drop an .svg in) plus the shared engine icon set.
        const std::filesystem::path roots[] = { Common::Constants::Path::ASSETS_PATH,
                                                Common::Constants::Path::ICONS_PATH };
        for ( const auto& root : roots )
        {
            std::error_code ec;
            for ( const auto& de : std::filesystem::recursive_directory_iterator( root, ec ) )
                if ( !ec && de.is_regular_file( ec ) && de.path().extension() == ".svg" )
                    RegisterIcon( de.path().generic_string() );
        }
        std::sort( m_Available.begin(), m_Available.end() );
        m_Available.erase( std::unique( m_Available.begin(), m_Available.end() ), m_Available.end() );
    }
} // namespace Desert::Runtime
