#include "FontService.hpp"

#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Runtime
{
    Font* FontService::Get( const std::string& ttfPath, float pixelHeight )
    {
        const std::string key = ttfPath + '|' + std::to_string( static_cast<int>( pixelHeight ) );
        if ( auto it = m_Fonts.find( key ); it != m_Fonts.end() )
            return it->second.get();

        const auto ttf = Common::Utils::FileSystem::ReadByteFileContent( ttfPath );
        if ( ttf.empty() )
        {
            LOG_ERROR( "[FontService] Cannot read font '{}'", ttfPath );
            return nullptr;
        }

        Text::BakedFont baked = Text::BakeFontSDF( ttf.data(), ttf.size(), pixelHeight );
        if ( !baked.Valid() )
        {
            LOG_ERROR( "[FontService] Failed to bake SDF atlas for '{}'", ttfPath );
            return nullptr;
        }

        // The engine's attachment/sampler formats are RGBA8 (no R8) — expand the single-channel SDF
        // into all four channels so the shader can read .r and a debug view still shows the atlas.
        std::vector<unsigned char> rgba( static_cast<size_t>( baked.AtlasWidth ) * baked.AtlasHeight * 4 );
        for ( size_t i = 0; i < baked.AtlasR8.size(); ++i )
        {
            const unsigned char v = baked.AtlasR8[i];
            rgba[i * 4 + 0]       = v;
            rgba[i * 4 + 1]       = v;
            rgba[i * 4 + 2]       = v;
            rgba[i * 4 + 3]       = v;
        }

        Core::Formats::Image2DSpecification spec = {
             .Tag        = "FontAtlas:" + key,
             .Width      = baked.AtlasWidth,
             .Height     = baked.AtlasHeight,
             .Format     = Core::Formats::ImageFormat::RGBA8F,
             .Mips       = 1,
             .Data       = std::move( rgba ),
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Sample };

        auto atlas = Graphic::Image2D::Create( spec, nullptr );
        if ( !atlas )
        {
            LOG_ERROR( "[FontService] GPU atlas upload failed for '{}'", ttfPath );
            return nullptr;
        }

        auto font   = std::make_unique<Font>();
        font->Atlas = std::move( atlas );
        font->Baked = std::move( baked );
        Font* raw   = font.get();
        m_Fonts[key] = std::move( font );
        LOG_INFO( "[FontService] Baked '{}' @ {}px -> {}x{} atlas", ttfPath, static_cast<int>( pixelHeight ),
                  raw->Baked.AtlasWidth, raw->Baked.AtlasHeight );
        return raw;
    }

    void FontService::Clear()
    {
        m_Fonts.clear();
    }
} // namespace Desert::Runtime
