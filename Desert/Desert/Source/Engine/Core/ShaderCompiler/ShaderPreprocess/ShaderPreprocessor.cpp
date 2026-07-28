#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>
#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>

#include <Common/Core/Core.hpp>

namespace Desert::Core::Preprocess
{
    // Every shader program is a single-file Desert Shader Language document (Shader "Name" { ... }).
    // The legacy multi-file '#pragma program/use_stage/param/state' format was fully migrated and its
    // parser removed — a non-DSL source is a hard error pointing at the migration, not a fallback.

    namespace
    {
        DShaderParseResult ParseOrDie( const std::string& source, const std::string& context )
        {
            DESERT_VERIFY( DShaderParser::IsDShader( source ),
                           "Not a DSL shader ({}). The legacy #pragma format is no longer supported — "
                           "rewrite as Shader \"Name\" {{ ... }}.",
                           context );
            auto parsed = DShaderParser::Parse( source );
            DESERT_VERIFY( parsed.IsSuccess(), "{} ({})", parsed.GetError(), context );
            return std::move( parsed.GetValue() );
        }
    } // namespace

    std::unordered_map<Desert::Core::Formats::ShaderStage, std::string>
    ShaderPreprocess::PreProcessProgramPass( const std::string& source, const std::filesystem::path& basePath,
                                             const std::string& passName )
    {
        auto        parsed = ParseOrDie( source, basePath.string() );
        const auto* pass   = parsed.FindPass( passName );
        DESERT_VERIFY( pass, "Shader has no pass named '{}' ({})", passName, basePath.string() );
        return pass->Stages;
    }

    Core::Formats::ShaderProgramMeta ShaderPreprocess::ParseProgramMetaForPass( const std::string& source,
                                                                                const std::string& passName )
    {
        auto        parsed = ParseOrDie( source, "meta" );
        const auto* pass   = parsed.FindPass( passName );
        DESERT_VERIFY( pass, "Shader has no pass named '{}'", passName );

        // A pass program: same params/domain, its own render state, and no sub-passes of its own
        // (so the ShaderService doesn't recurse when registering).
        Core::Formats::ShaderProgramMeta meta = parsed.Meta;
        meta.State                            = pass->State;
        if ( !passName.empty() )
            meta.PassNames.clear();
        return meta;
    }

    std::unordered_map<Desert::Core::Formats::ShaderStage, std::string>
    ShaderPreprocess::PreProcessProgram( const std::string& source, const std::filesystem::path& basePath )
    {
        return std::move( ParseOrDie( source, basePath.string() ).Stages );
    }

    Core::Formats::ShaderProgramMeta ShaderPreprocess::ParseProgramMeta( const std::string& source )
    {
        return std::move( ParseOrDie( source, "meta" ).Meta );
    }
} // namespace Desert::Core::Preprocess
