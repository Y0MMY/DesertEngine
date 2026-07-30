#include "FontService.hpp"

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <filesystem>
#include <format>
#include <fstream>

namespace Desert::Runtime
{
    namespace
    {
        // Content-addressed cache key: a version tag + the TTF bytes + the bake size. Any font-file edit or
        // size change produces a fresh key, so the on-disk cache never goes stale (FNV-1a, same scheme as the
        // SPIR-V shader cache). The baker's other params (padding/atlasWidth) are compile-time constants folded
        // into kBakedFontCacheVersion, so bumping that version alone invalidates every cached atlas.
        uint64_t FontCacheKey( const std::vector<uint8_t>& ttf, float pixelHeight )
        {
            constexpr uint64_t kFnvOffset = 1469598103934665603ull;
            constexpr uint64_t kFnvPrime  = 1099511628211ull;

            uint64_t h = kFnvOffset;
            h ^= Text::kBakedFontCacheVersion;
            h *= kFnvPrime;
            h ^= static_cast<uint64_t>( static_cast<int>( pixelHeight ) );
            h *= kFnvPrime;
            for ( uint8_t b : ttf )
            {
                h ^= b;
                h *= kFnvPrime;
            }
            return h;
        }

        std::filesystem::path FontCachePath( uint64_t key )
        {
            return Common::Constants::Path::COOKED_PATH / "FontCache" / std::format( "{:016x}.dfont", key );
        }

        bool TryLoadBakedFont( const std::filesystem::path& path, Text::BakedFont& out )
        {
            std::error_code ec;
            const auto      size = std::filesystem::file_size( path, ec );
            if ( ec || size == 0 )
                return false;
            std::ifstream in( path, std::ios::binary );
            if ( !in )
                return false;
            std::vector<uint8_t> bytes( static_cast<size_t>( size ) );
            in.read( reinterpret_cast<char*>( bytes.data() ), static_cast<std::streamsize>( size ) );
            if ( !in )
                return false;
            return Text::DeserializeBakedFont( bytes.data(), bytes.size(), out );
        }

        void StoreBakedFont( const std::filesystem::path& path, const Text::BakedFont& font )
        {
            std::error_code ec;
            std::filesystem::create_directories( path.parent_path(), ec );
            std::ofstream out( path, std::ios::binary | std::ios::trunc );
            if ( !out ) // read-only install (e.g. inside an .app bundle) — cache is best-effort
                return;
            const std::vector<uint8_t> bytes = Text::SerializeBakedFont( font );
            out.write( reinterpret_cast<const char*>( bytes.data() ),
                       static_cast<std::streamsize>( bytes.size() ) );
        }
    } // namespace

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

        // Disk cache: skip the (CPU-bound) SDF bake if a matching atlas was cooked on a previous run.
        const std::filesystem::path cachePath = FontCachePath( FontCacheKey( ttf, pixelHeight ) );
        Text::BakedFont             baked;
        const bool                  fromCache = TryLoadBakedFont( cachePath, baked );
        if ( !fromCache )
        {
            baked = Text::BakeFontSDF( ttf.data(), ttf.size(), pixelHeight );
            if ( !baked.Valid() )
            {
                LOG_ERROR( "[FontService] Failed to bake SDF atlas for '{}'", ttfPath );
                return nullptr;
            }
            StoreBakedFont( cachePath, baked );
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
        LOG_INFO( "[FontService] {} '{}' @ {}px -> {}x{} atlas", fromCache ? "Loaded cached" : "Baked", ttfPath,
                  static_cast<int>( pixelHeight ), raw->Baked.AtlasWidth, raw->Baked.AtlasHeight );
        return raw;
    }

    void FontService::Clear()
    {
        m_Fonts.clear();
    }
} // namespace Desert::Runtime
