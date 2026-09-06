#include "PackageCook.hpp"

#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>
#include <Engine/Core/ShaderCompiler/ShaderCacheKey.hpp>
#include <Engine/Core/ShaderCompiler/ShaderCompiler.hpp>
#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>
#include <Engine/Core/ShaderCompiler/ShaderSpirvCache.hpp>
#include <Engine/Runtime/Services/ServiceScanRoots.hpp>
#include <Engine/Text/FontCache.hpp>
#include <Engine/Vector/IconBake.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace Desert::Editor
{
    namespace fs = std::filesystem;

    namespace
    {
        // The same enumeration + extension filter AssetPreloader::PreloadShaders uses (lowercased
        // extension over ListFilesRecursive of the live SHADERDIR_PATH): the cook must see exactly
        // the set of shaders the runtime will register, or a shader the runtime compiles at startup
        // is one the cook silently skipped.
        std::vector<fs::path> ShippedShaderFiles()
        {
            std::vector<fs::path> out;
            for ( const auto& candidate :
                  Common::Utils::FileSystem::ListFilesRecursive( Common::Constants::Path::SHADERDIR_PATH ) )
            {
                std::string ext = candidate.extension().string();
                std::transform( ext.begin(), ext.end(), ext.begin(), ::tolower );
                if ( ext == ".shader" )
                    out.push_back( candidate );
            }
            return out;
        }

        void CookShaders( bool spirvDebugInfo, CookStats& stats )
        {
            namespace Preprocess = Core::Preprocess;

            for ( const fs::path& file : ShippedShaderFiles() )
            {
                const std::string content = Common::Utils::FileSystem::ReadFileContent( file );
                if ( content.empty() )
                {
                    ++stats.Failures; // ReadFileContent already logged the miss
                    continue;
                }

                // Pre-check with the parser proper: PreProcessProgramPass aborts (DESERT_VERIFY) on
                // an unparsable file, and a broken .shader must fail THIS shader's cook, not the
                // whole packaging run.
                if ( const auto parsed = Preprocess::DShaderParser::Parse( content ); !parsed.IsSuccess() )
                {
                    LOG_ERROR( "[PackageCook] {} does not parse and was not cooked: {}", file.string(),
                               parsed.GetError() );
                    ++stats.Failures;
                    continue;
                }

                // The default program plus every named pass — the same set ShaderService::Register
                // turns into programs at startup.
                std::vector<std::string> passes = { "" };
                const auto meta = Preprocess::ShaderPreprocess::ParseProgramMetaForPass( content, "" );
                passes.insert( passes.end(), meta.PassNames.begin(), meta.PassNames.end() );

                for ( const std::string& passName : passes )
                {
                    const auto stages =
                         Preprocess::ShaderPreprocess::PreProcessProgramPass( content, file, passName );
                    for ( const auto& [stage, source] : stages )
                    {
                        const uint64_t key =
                             Core::ComputeShaderCacheKeyForProfile( stage, source, file, spirvDebugInfo );
                        if ( Core::TryLoadCachedSpirv( key ) )
                        {
                            ++stats.ShadersCached;
                            continue;
                        }
                        // Compiles under the SAME key (same inputs, same profile) and stores it.
                        if ( Core::ShaderCompiler::CompileGLSLToSPIRVForProfile( stage, source, file.string(),
                                                                                 spirvDebugInfo )
                                  .IsSuccess() )
                            ++stats.ShadersCompiled;
                        else
                            ++stats.Failures; // the compiler logged file/stage/diagnostic
                    }
                }
            }
        }

        void CookFonts( CookStats& stats )
        {
            for ( const fs::path* root : Runtime::FontScanRoots() )
            {
                for ( const auto& p : Common::Utils::FileSystem::ListFilesRecursive( *root ) )
                {
                    if ( p.extension() != ".ttf" ) // FontService::EnsurePreloaded's own filter
                        continue;

                    const auto ttf = Common::Utils::FileSystem::ReadByteFileContent( p );
                    if ( ttf.empty() )
                    {
                        ++stats.Failures;
                        continue;
                    }

                    // The bake the runtime asks for on a text-bearing first frame: the default size,
                    // ASCII only. A scene using glyphs beyond ASCII re-bakes once at runtime (a
                    // different glyph set is a different key by design); the shipped default covers
                    // the common case and the default font.
                    const uint64_t  key = Text::FontCacheKey( ttf, Text::kDefaultBakePixelHeight, {} );
                    Text::BakedFont existing;
                    if ( Text::TryLoadBakedFont( Text::FontCachePath( key ), existing ) )
                    {
                        ++stats.FontsCached;
                        continue;
                    }

                    const Text::BakedFont baked = Text::BakeFontForCache( ttf, Text::kDefaultBakePixelHeight, {} );
                    if ( !baked.Valid() )
                    {
                        LOG_ERROR( "[PackageCook] {} could not be baked into an SDF atlas", p.string() );
                        ++stats.Failures;
                        continue;
                    }
                    Text::StoreBakedFont( Text::FontCachePath( key ), baked );
                    ++stats.FontsBaked;
                }
            }
        }

        void CookIcons( CookStats& stats )
        {
            for ( const fs::path* root : Runtime::IconScanRoots() )
            {
                for ( const auto& p : Common::Utils::FileSystem::ListFilesRecursive( *root ) )
                {
                    if ( p.extension() != ".svg" ) // IconService::EnsurePreloaded's own filter
                        continue;

                    const auto svg = Common::Utils::FileSystem::ReadByteFileContent( p );
                    if ( svg.empty() )
                    {
                        ++stats.Failures;
                        continue;
                    }

                    const uint64_t    key = Vector::IconCacheKey( svg );
                    Vector::BakedIcon existing;
                    if ( Vector::TryLoadBakedIcon( Vector::IconCachePath( key ), existing ) )
                    {
                        ++stats.IconsCached;
                        continue;
                    }

                    const Vector::BakedIcon baked = Vector::BakeIconSdf( svg.data(), svg.size() );
                    if ( !baked.Valid() )
                    {
                        LOG_ERROR( "[PackageCook] {} has no filled shapes the icon importer understands",
                                   p.string() );
                        ++stats.Failures;
                        continue;
                    }
                    Vector::StoreBakedIcon( Vector::IconCachePath( key ), baked );
                    ++stats.IconsBaked;
                }
            }
        }
    } // namespace

    CookStats CookContentCaches( bool spirvDebugInfo )
    {
        CookStats stats;
        CookShaders( spirvDebugInfo, stats );
        CookFonts( stats );
        CookIcons( stats );

        LOG_INFO( "[PackageCook] shaders {} compiled / {} cached, fonts {} baked / {} cached, icons {} "
                  "baked / {} cached, {} failure(s)",
                  stats.ShadersCompiled, stats.ShadersCached, stats.FontsBaked, stats.FontsCached,
                  stats.IconsBaked, stats.IconsCached, stats.Failures );
        return stats;
    }
} // namespace Desert::Editor
