#include <variant>
#include "TextureImporter.hpp"

#include <mutex>
#include "CookPaths.hpp"

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
        // Path formula is shared (CookPaths::CookedTexture); this wrapper ensures the dir exists for writing.
        const auto result = Editor::CookPaths::CookedTexture( sourcePath, extension );
        std::filesystem::create_directories( result.parent_path() );
        return result;
    }

    std::filesystem::path TextureImporter::CookedMetaPath( const std::filesystem::path& source )
    {
        return BuildCookedPath( source, ".tex" );
    }

    namespace
    {
        // Deterministic 64-bit id (FNV-1a) from a stable key -> Common::UUID. Used so a texture's handle is
        // DERIVED from its source path, not random: deleting Cooked/ and re-cooking yields the SAME handle, so
        // every material/.demat/scene reference keyed by it still resolves (random handles broke on a Cooked
        // wipe). Mirrors AssimpImporter::StableMaterialId.
        Common::UUID StableTextureId( const std::string& key )
        {
            uint64_t h = 1469598103934665603ull;
            for ( unsigned char c : key )
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            if ( h == 0 )
                h = 1; // never collide with the null handle
            return Common::UUID( h );
        }
    } // namespace

    Common::UUID TextureImporter::Import( const std::filesystem::path& path )
    {
        // Bulk cooking runs mesh imports in PARALLEL; two meshes often share textures, and two threads
        // writing the same cooked .tex would corrupt it. Texture cooking is cheap next to the Assimp
        // parse, so one global lock here is the simplest safe answer.
        static std::mutex           s_CookMutex;
        std::lock_guard<std::mutex> cookLock( s_CookMutex );

        auto abs = std::filesystem::weakly_canonical( path ).string();

        if ( m_Cache.contains( abs ) )
        {
            return m_Cache[abs];
        }

        const auto meta = BuildCookedPath( path, ".tex" );

        // Deterministic default so the handle survives a Cooked/ wipe (re-cook -> same id). If a .tex already
        // exists, PRESERVE whatever handle it stored (back-compat with older random-handle cooks); if it's
        // also up-to-date, skip rewriting.
        Common::UUID    handle = StableTextureId( abs );
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