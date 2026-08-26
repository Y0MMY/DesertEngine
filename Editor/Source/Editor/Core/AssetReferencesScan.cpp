#include "AssetReferences.hpp"

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <Engine/Project/ProjectContext.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Desert::Editor
{
    namespace
    {
        std::string Lower( std::string s )
        {
            std::transform( s.begin(), s.end(), s.begin(),
                            []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            return s;
        }

        // Formats whose text we scan for references. Binaries (textures, cooked meshes) are still
        // indexed as reference TARGETS, they just contribute no searchable text.
        bool IsTextAsset( const std::string& ext )
        {
            static const std::vector<std::string> kText = { ".demat",  ".desce", ".deprefab", ".dgraph",
                                                            ".deproj", ".desky", ".decol",    ".json",
                                                            ".lua",    ".shader", ".glslh" };
            return std::find( kText.begin(), kText.end(), ext ) != kText.end();
        }

        std::string HandleToken( const std::filesystem::path& p )
        {
            return std::to_string( static_cast<uint64_t>( Common::AssetHandle::FromCookedPath( p ) ) );
        }

        // Pull self-identifier fields like "MaterialId":<digits> / "MeshId":<digits> — the value a
        // scene/prefab embeds to reference this asset. Bare per-entity "Id": is excluded (the char
        // before "Id" must be a letter), and only long (handle-sized) numbers are kept.
        void ExtractSelfIds( const std::string& text, std::vector<std::string>& tokens )
        {
            const std::string key = "Id\":";
            for ( std::size_t pos = text.find( key ); pos != std::string::npos;
                  pos            = text.find( key, pos + 1 ) )
            {
                if ( pos == 0 || !std::isalpha( static_cast<unsigned char>( text[pos - 1] ) ) )
                    continue; // bare "Id": (entity id), not an asset self-id
                std::size_t i = pos + key.size();
                std::string num;
                while ( i < text.size() && std::isdigit( static_cast<unsigned char>( text[i] ) ) )
                    num += text[i++];
                if ( num.size() >= 10 )
                    tokens.push_back( num );
            }
        }
    } // namespace

    void BuildProjectAssetReferenceIndex( AssetReferenceIndex& index )
    {
        index.Clear();
        if ( !::Desert::Project::ProjectContext::HasProject() )
            return;

        namespace fs              = std::filesystem;
        const fs::path assetsRoot = Common::Constants::Path::ASSETS_PATH;
        const fs::path projectDir = ::Desert::Project::ProjectContext::Directory();

        std::error_code ec;
        for ( const auto& de : fs::recursive_directory_iterator( assetsRoot, ec ) )
        {
            if ( ec )
                break;
            if ( !de.is_regular_file( ec ) )
                continue;
            const fs::path& p = de.path();

            AssetReferenceIndex::Entry e;
            e.Path = fs::relative( p, assetsRoot, ec ).generic_string();
            e.Ext  = Lower( p.extension().string() );

            // Every way a referencer might name this asset: its handle, the project-relative path string,
            // and the file name.
            //
            // ONE handle token, not three. This used to hash the project-relative, assets-relative and
            // absolute spellings, because the derivation keyed on the string it was handed and each
            // spelling produced a different number. It no longer does: an asset's handle is a function of
            // its place in the project, so the absolute form below IS the handle, and the other two were
            // not merely redundant -- absolutized against the working directory, the assets-relative
            // spelling lands outside every root and hashes to a value no producer in the engine can mint.
            // A token that matches nothing is a token that hides the one that matches.
            const std::string projectRel = fs::relative( p, projectDir, ec ).generic_string();
            e.Tokens.push_back( HandleToken( p.lexically_normal() ) );
            e.Tokens.push_back( projectRel );
            e.Tokens.push_back( p.filename().generic_string() );

            if ( IsTextAsset( e.Ext ) )
            {
                e.Text = Common::Utils::FileSystem::ReadFileContent( p.string() );
                ExtractSelfIds( e.Text, e.Tokens );
            }

            std::sort( e.Tokens.begin(), e.Tokens.end() );
            e.Tokens.erase( std::unique( e.Tokens.begin(), e.Tokens.end() ), e.Tokens.end() );

            index.Add( std::move( e ) );
        }
    }
} // namespace Desert::Editor
