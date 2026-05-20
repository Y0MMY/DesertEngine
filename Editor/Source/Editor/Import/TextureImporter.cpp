#include <variant>
#include "TextureImporter.hpp"

#include <Common/Core/Serialization/GlmReflection.hpp>
#include <regex>

#include <Engine/Assets/Serialization/Texture.hpp>

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

    Common::UUID TextureImporter::Import( const std::filesystem::path& path )
    {
        auto abs = std::filesystem::weakly_canonical( path ).string();

        if ( m_Cache.contains( abs ) )
        {
            return m_Cache[abs];
        }

        Common::UUID handle{};

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

        auto meta = BuildCookedPath( path, ".tex" );
        WriteJsonToFile( data, meta );

        m_Cache[abs] = handle;

        return handle;
    }

} // namespace Desert::Editor