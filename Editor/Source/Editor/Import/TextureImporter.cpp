#include <variant>
#include "TextureImporter.hpp"

#include <Common/Core/Serialization/GlmReflection.hpp>
#include <regex>

#include <Engine/Assets/Serialization/Texture.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <stb_image/stb_image.h>

namespace Desert::Editor
{
    template <typename T>
    void WriteJsonToFile( const T& data, const std::filesystem::path& path )
    {
        auto json = rfl::json::write( data );

        static const std::regex illegal( R"([<>:"/\\|?*])" );

        std::filesystem::create_directories( path.parent_path() );

        std::string filename = path.filename().string();
        filename             = std::regex_replace( filename, illegal, "_" );

        std::filesystem::path fixedPath = path.parent_path() / filename;

        std::ofstream out( fixedPath, std::ios::binary );
        if ( !out.is_open() )
        {
            throw std::runtime_error( "Failed to open file: " + fixedPath.string() );
        }

        out << json;
    }

    static std::filesystem::path BuildCookedPath( const std::filesystem::path& sourcePath,
                                                  const std::string&           extension )
    {
        namespace fs = std::filesystem;

        fs::path relative = fs::relative( sourcePath, "Resources/Textures/" );

        fs::path cookedRoot = "Cooked/Textures";

        fs::path result = cookedRoot / relative;
        result.replace_extension( extension );

        fs::create_directories( result.parent_path() );

        return result;
    }

    std::filesystem::path TextureImporter::CookedMetaPath( const std::filesystem::path& source )
    {
        return BuildCookedPath( source, ".tex" );
    }

    Common::UUID TextureImporter::Import( const std::filesystem::path& path )
    {
        auto abs = std::filesystem::weakly_canonical( path ).string();

        if ( m_Cache.contains( abs ) )
        {
            return m_Cache[abs];
        }

        const auto meta = BuildCookedPath( path, ".tex" );

        // Stable handles: if this texture was cooked before, reuse the handle stored in the existing .tex
        // (so material/scene references survive re-cooks). If the .tex is also up-to-date, skip rewriting.
        Common::UUID    handle{};
        std::error_code ec;
        if ( std::filesystem::exists( meta, ec ) )
        {
            const auto existing = rfl::json::read<Assets::Serialization::TextureAssetData>(
                 Common::Utils::FileSystem::ReadFileContent( meta ) );
            if ( existing.has_value() )
            {
                handle = existing->Handle;

                std::error_code ec2;
                const auto      metaT = std::filesystem::last_write_time( meta, ec2 );
                const auto      srcT  = std::filesystem::last_write_time( path, ec2 );
                if ( !ec2 && metaT >= srcT )
                {
                    m_Cache[abs] = handle; // up-to-date — keep the existing cooked metadata as-is
                    return handle;
                }
            }
        }

        Assets::Serialization::TextureAssetData data;
        data.Handle     = handle;
        data.SourcePath = abs;

        int      w, h, ch;
        stbi_uc* pixels = stbi_load( abs.c_str(), &w, &h, &ch, 4 );

        data.Width    = w;
        data.Height   = h;
        data.Channels = 4;
        data.Format   = Desert::Core::Formats::ImageFormat::RGBA8F;

        auto cooked     = BuildCookedPath( path, ".dds" );
        data.CookedPath = cooked.string();

        stbi_image_free( pixels );

        WriteJsonToFile( data, meta );

        m_Cache[abs] = handle;

        return handle;
    }

} // namespace Desert::Editor