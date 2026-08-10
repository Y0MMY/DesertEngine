#include "ShaderCacheKey.hpp"

#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <unordered_set>

namespace Desert::Core
{
    namespace
    {
        constexpr uint64_t kFnvOffset = 1469598103934665603ull;
        constexpr uint64_t kFnvPrime  = 1099511628211ull;

        // Bumped when anything about how a stage is COMPILED changes (target environment, debug info,
        // warning policy) without the source changing. Without it a compiler-option change would keep
        // serving artifacts built under the old options.
        constexpr const char* kOptionsFingerprint = "vulkan1.1|v1";

        void FnvMix( uint64_t& h, std::string_view data )
        {
            for ( unsigned char c : data )
            {
                h ^= c;
                h *= kFnvPrime;
            }
        }

        // The include walk, shared by the collector and the hash. `visited` carries across the whole
        // traversal, so a header pulled in by two different files is listed (and hashed) once — which
        // is also what stops a cycle from recursing forever.
        void WalkIncludes( const std::string& source, const std::filesystem::path& requestingFile,
                           std::unordered_set<std::string>& visited, std::vector<std::filesystem::path>& out,
                           int depth )
        {
            if ( depth > 32 )
                return;

            size_t pos = 0;
            while ( ( pos = source.find( "#include", pos ) ) != std::string::npos )
            {
                const size_t      lineEnd = source.find( '\n', pos );
                const std::string line =
                     source.substr( pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos );
                pos += 8;

                const size_t qa = line.find_first_of( "\"<" );
                if ( qa == std::string::npos )
                    continue;
                const char   closer = line[qa] == '"' ? '"' : '>';
                const size_t qb     = line.find( closer, qa + 1 );
                if ( qb == std::string::npos )
                    continue;
                const std::string name = line.substr( qa + 1, qb - qa - 1 );

                const std::filesystem::path full =
                     ( line[qa] == '"' ? requestingFile.parent_path() / name
                                       : Common::Constants::Path::SHADERDIR_PATH / name )
                          .lexically_normal();

                if ( !visited.insert( full.generic_string() ).second )
                    continue;
                if ( !Common::Utils::FileSystem::Exists( full ) )
                    continue;

                out.push_back( full );
                WalkIncludes( Common::Utils::FileSystem::ReadFileContent( full ), full, visited, out, depth + 1 );
            }
        }
    } // namespace

    std::vector<std::filesystem::path> CollectShaderIncludes( const std::string&           source,
                                                              const std::filesystem::path& requestingFile )
    {
        std::unordered_set<std::string>    visited;
        std::vector<std::filesystem::path> includes;
        WalkIncludes( source, requestingFile, visited, includes, 0 );
        return includes;
    }

    uint64_t ComputeShaderCacheKey( Formats::ShaderStage stage, const std::string& source,
                                    const std::filesystem::path& requestingFile )
    {
        uint64_t key = kFnvOffset;
        FnvMix( key, kOptionsFingerprint );
#ifdef DESERT_CONFIG_DEBUG
        FnvMix( key, "|debuginfo" ); // debug info changes the binary — keep configs apart
#endif
        key ^= static_cast<uint64_t>( stage );
        key *= kFnvPrime;
        FnvMix( key, source );

        // Path AND content of every include: the path so that moving a header to a different directory
        // is a change even when the bytes are identical, the content so that editing it is one too.
        for ( const auto& include : CollectShaderIncludes( source, requestingFile ) )
        {
            FnvMix( key, include.generic_string() );
            FnvMix( key, Common::Utils::FileSystem::ReadFileContent( include ) );
        }

        return key;
    }
} // namespace Desert::Core
