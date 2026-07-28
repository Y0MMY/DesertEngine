#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>

#include <sstream>
#include <optional>
#include <vector>
#include <string>

namespace Desert::Core::Preprocess
{
    namespace
    {
        Formats::ShaderStage ParseShaderType( const std::string& typeStr )
        {
            if ( typeStr == "vertex" )
                return Formats::ShaderStage::Vertex;
            if ( typeStr == "fragment" )
                return Formats::ShaderStage::Fragment;
            if ( typeStr == "compute" )
                return Formats::ShaderStage::Compute;
            if ( typeStr == "tess_control" || typeStr == "tesc" )
                return Formats::ShaderStage::TessControl;
            if ( typeStr == "tess_evaluation" || typeStr == "tese" )
                return Formats::ShaderStage::TessEvaluation;

            return Formats::ShaderStage::None;
        }

        // --- Helpers for #pragma param / #pragma state parsing ---

        // Extracts the contents of a `key(...)` group from a line (e.g. range(0,1) -> "0,1"). Returns
        // std::nullopt when the key is absent.
        std::optional<std::string> ExtractGroup( const std::string& line, const std::string& key )
        {
            const std::string token = key + "(";
            const auto        pos   = line.find( token );
            if ( pos == std::string::npos )
                return std::nullopt;
            const auto open  = pos + token.size();
            const auto close = line.find( ')', open );
            if ( close == std::string::npos )
                return std::nullopt;
            return line.substr( open, close - open );
        }

        // Splits "1, 0.5 ,2" -> {1, 0.5, 2}, ignoring surrounding quotes/whitespace.
        std::vector<float> ParseFloatList( const std::string& csv )
        {
            std::vector<float> out;
            std::stringstream  ss( csv );
            std::string        tok;
            while ( std::getline( ss, tok, ',' ) )
            {
                size_t b = tok.find_first_not_of( " \t\"" );
                size_t e = tok.find_last_not_of( " \t\"" );
                if ( b == std::string::npos )
                    continue;
                try
                {
                    out.push_back( std::stof( tok.substr( b, e - b + 1 ) ) );
                }
                catch ( ... )
                {
                }
            }
            return out;
        }

        // Maps a `#pragma param` type token to a value type + UI/texture flags.
        void MapParamType( const std::string& typeStr, Formats::ShaderParam& param )
        {
            using VT = Formats::ShaderValueType;
            using W  = Formats::ShaderParamWidget;

            if ( typeStr == "float" )
                param.Type = VT::Float;
            else if ( typeStr == "vec2" )
                param.Type = VT::Float2;
            else if ( typeStr == "vec3" )
                param.Type = VT::Float3;
            else if ( typeStr == "vec4" )
                param.Type = VT::Float4;
            else if ( typeStr == "int" )
                param.Type = VT::Int;
            else if ( typeStr == "bool" )
                param.Type = VT::Bool;
            else if ( typeStr == "color" || typeStr == "color4" )
            {
                param.Type   = VT::Float4;
                param.Widget = W::Color;
            }
            else if ( typeStr == "color3" )
            {
                param.Type   = VT::Float3;
                param.Widget = W::Color;
            }
            else if ( typeStr == "texture2D" || typeStr == "textureCube" )
            {
                param.IsTexture = true;
                param.Type      = VT::Unknown;
            }
        }

        Formats::StateCull ParseCull( const std::string& s )
        {
            if ( s == "front" )
                return Formats::StateCull::Front;
            if ( s == "back" )
                return Formats::StateCull::Back;
            if ( s == "frontandback" )
                return Formats::StateCull::FrontAndBack;
            return Formats::StateCull::None;
        }

        std::optional<Formats::StateCompare> ParseCompare( const std::string& s )
        {
            using C = Formats::StateCompare;
            if ( s == "never" )
                return C::Never;
            if ( s == "less" )
                return C::Less;
            if ( s == "equal" )
                return C::Equal;
            if ( s == "lessorequal" || s == "lequal" )
                return C::LessOrEqual;
            if ( s == "greater" )
                return C::Greater;
            if ( s == "notequal" )
                return C::NotEqual;
            if ( s == "greaterorequal" || s == "gequal" )
                return C::GreaterOrEqual;
            if ( s == "always" )
                return C::Always;
            return std::nullopt;
        }

    } // namespace

    std::unordered_map<Desert::Core::Formats::ShaderStage, std::string>
    ShaderPreprocess::PreProcessProgramPass( const std::string& source, const std::filesystem::path& basePath,
                                             const std::string& passName )
    {
        if ( DShaderParser::IsDShader( source ) )
        {
            auto parsed = DShaderParser::Parse( source );
            DESERT_VERIFY( parsed.IsSuccess(), "{} ({})", parsed.GetError(), basePath.string() );

            const auto* pass = parsed.GetValue().FindPass( passName );
            DESERT_VERIFY( pass, "Shader has no pass named '{}' ({})", passName, basePath.string() );
            return pass->Stages;
        }

        DESERT_VERIFY( passName.empty(), "Legacy #pragma shaders have no passes (requested '{}', {})",
                       passName, basePath.string() );
        return PreProcessProgram( source, basePath );
    }

    Core::Formats::ShaderProgramMeta ShaderPreprocess::ParseProgramMetaForPass( const std::string& source,
                                                                                const std::string& passName )
    {
        if ( DShaderParser::IsDShader( source ) )
        {
            auto parsed = DShaderParser::Parse( source );
            DESERT_VERIFY( parsed.IsSuccess(), "{}", parsed.GetError() );

            const auto& value = parsed.GetValue();
            const auto* pass  = value.FindPass( passName );
            DESERT_VERIFY( pass, "Shader has no pass named '{}'", passName );

            // A pass program: same params/domain, its own render state, and no sub-passes of
            // its own (so the ShaderService doesn't recurse when registering).
            Core::Formats::ShaderProgramMeta meta = value.Meta;
            meta.State                            = pass->State;
            if ( !passName.empty() )
                meta.PassNames.clear();
            return meta;
        }

        DESERT_VERIFY( passName.empty(), "Legacy #pragma shaders have no passes (requested '{}')", passName );
        return ParseProgramMeta( source );
    }

    std::unordered_map<Desert::Core::Formats::ShaderStage, std::string>
    ShaderPreprocess::PreProcessProgram( const std::string& source, const std::filesystem::path& basePath )
    {
        using namespace Desert::Core::Formats;

        // Single-file DSL format (Shader "Name" { ... }) — stages are embedded blocks.
        if ( DShaderParser::IsDShader( source ) )
        {
            auto parsed = DShaderParser::Parse( source );
            DESERT_VERIFY( parsed.IsSuccess(), "{} ({})", parsed.GetError(), basePath.string() );
            return std::move( parsed.GetValue().Stages );
        }

        std::unordered_map<ShaderStage, std::string> programStages;

        std::istringstream stream( source );
        std::string        line;

        while ( std::getline( stream, line ) )
        {
            if ( line.find( "#pragma use_stage" ) == std::string::npos )
                continue;

            // template:
            // #pragma use_stage vertex "Static.vert.glsl"

            std::istringstream lineStream( line );
            std::string        pragma, keyword, stageStr, fileStr;

            lineStream >> pragma >> keyword >> stageStr >> fileStr;

            if ( pragma != "#pragma" || keyword != "use_stage" )
                continue;

            ShaderStage stage = ParseShaderType( stageStr );
            DESERT_VERIFY( stage != ShaderStage::None, "Unknown shader stage in use_stage" );

            if ( !fileStr.empty() && fileStr.front() == '"' )
                fileStr.erase( 0, 1 );
            if ( !fileStr.empty() && fileStr.back() == '"' )
                fileStr.pop_back();

            std::filesystem::path stagePath = ( basePath.parent_path() / fileStr ).lexically_normal();

            // FileSystem is VFS-aware: stage sources resolve from disk in dev and from the mounted
            // .dpak in a packaged game.
            DESERT_VERIFY( Common::Utils::FileSystem::Exists( stagePath ),
                           "Shader stage file not found: {}", stagePath.string() );

            std::string stageSource = Common::Utils::FileSystem::ReadFileContent( stagePath );

            DESERT_VERIFY( programStages.find( stage ) == programStages.end(),
                           "Duplicate shader stage in program: {}", Shader::GetStringShaderStage( stage ) );

            programStages.emplace( stage, std::move( stageSource ) );
        }

        DESERT_VERIFY( !programStages.empty(), "Shader program contains no stages" );

        return programStages;
    }

    Core::Formats::ShaderProgramMeta ShaderPreprocess::ParseProgramMeta( const std::string& source )
    {
        using namespace Desert::Core::Formats;

        // Single-file DSL format — Properties/State/Domain come from the structured blocks.
        if ( DShaderParser::IsDShader( source ) )
        {
            auto parsed = DShaderParser::Parse( source );
            DESERT_VERIFY( parsed.IsSuccess(), "{}", parsed.GetError() );
            return std::move( parsed.GetValue().Meta );
        }

        ShaderProgramMeta meta;

        std::istringstream stream( source );
        std::string        line;

        while ( std::getline( stream, line ) )
        {
            // ---- #pragma param <type> <Name> ["DisplayName"] [range(a,b)] [default(...)] [category("..")]
            if ( line.find( "#pragma param" ) != std::string::npos )
            {
                std::istringstream ls( line );
                std::string        pragma, keyword, typeStr, name;
                ls >> pragma >> keyword >> typeStr >> name;
                if ( pragma != "#pragma" || keyword != "param" || typeStr.empty() || name.empty() )
                    continue;

                ShaderParam param;
                param.Name = name;
                MapParamType( typeStr, param );

                // Optional quoted display name: the first "..." that is NOT part of a default("..") group.
                {
                    const auto q1 = line.find( '"' );
                    if ( q1 != std::string::npos )
                    {
                        const auto defPos = line.find( "default(" );
                        // accept the quote only if it appears before a default(...) group (or there is none)
                        if ( defPos == std::string::npos || q1 < defPos )
                        {
                            const auto q2 = line.find( '"', q1 + 1 );
                            if ( q2 != std::string::npos )
                                param.DisplayName = line.substr( q1 + 1, q2 - q1 - 1 );
                        }
                    }
                }
                if ( param.DisplayName.empty() )
                    param.DisplayName = param.Name;

                if ( auto cat = ExtractGroup( line, "category" ) )
                {
                    // strip quotes/whitespace
                    size_t b = cat->find_first_not_of( " \t\"" );
                    size_t e = cat->find_last_not_of( " \t\"" );
                    if ( b != std::string::npos )
                        param.Category = cat->substr( b, e - b + 1 );
                }

                if ( auto rng = ExtractGroup( line, "range" ) )
                {
                    const auto vals = ParseFloatList( *rng );
                    if ( vals.size() >= 2 )
                    {
                        param.Min = vals[0];
                        param.Max = vals[1];
                        if ( param.Widget == ShaderParamWidget::Auto )
                            param.Widget = ShaderParamWidget::Slider;
                    }
                }

                if ( auto def = ExtractGroup( line, "default" ) )
                {
                    if ( param.IsTexture )
                    {
                        size_t b = def->find_first_not_of( " \t\"" );
                        size_t e = def->find_last_not_of( " \t\"" );
                        if ( b != std::string::npos )
                            param.DefaultTexture = def->substr( b, e - b + 1 );
                    }
                    else
                    {
                        const auto vals = ParseFloatList( *def );
                        for ( size_t i = 0; i < vals.size() && i < 4; ++i )
                            param.Default[static_cast<glm::length_t>( i )] = vals[i];
                    }
                }

                meta.Params.push_back( std::move( param ) );
                continue;
            }

            // ---- #pragma domain <surface|terrain|skybox|postprocess>
            if ( line.find( "#pragma domain" ) != std::string::npos )
            {
                std::istringstream ls( line );
                std::string        pragma, keyword, domainStr;
                ls >> pragma >> keyword >> domainStr;
                if ( pragma == "#pragma" && keyword == "domain" )
                {
                    if ( domainStr == "surface" )
                        meta.Domain = ShaderDomain::Surface;
                    else if ( domainStr == "terrain" )
                        meta.Domain = ShaderDomain::Terrain;
                    else if ( domainStr == "skybox" )
                        meta.Domain = ShaderDomain::Skybox;
                    else if ( domainStr == "postprocess" )
                        meta.Domain = ShaderDomain::PostProcess;
                }
                continue;
            }

            // ---- #pragma state ...
            if ( line.find( "#pragma state" ) != std::string::npos )
            {
                std::istringstream ls( line );
                std::string        pragma, keyword;
                ls >> pragma >> keyword;
                if ( pragma != "#pragma" || keyword != "state" )
                    continue;

                std::vector<std::string> toks;
                std::string              t;
                while ( ls >> t )
                    toks.push_back( t );
                if ( toks.empty() )
                    continue;

                const std::string& cmd = toks[0];
                if ( cmd == "cull" && toks.size() >= 2 )
                {
                    meta.State.Cull = ParseCull( toks[1] );
                }
                else if ( cmd == "blend" && toks.size() >= 2 )
                {
                    meta.State.Blend = ( toks[1] == "on" || toks[1] == "true" );
                }
                else if ( cmd == "depth" )
                {
                    // forms: "depth off", "depth <compareop> [write on|off]", "depth write on|off"
                    for ( size_t i = 1; i < toks.size(); ++i )
                    {
                        if ( toks[i] == "off" && i == 1 )
                        {
                            meta.State.DepthTest = false;
                        }
                        else if ( toks[i] == "on" && i == 1 )
                        {
                            meta.State.DepthTest = true;
                        }
                        else if ( toks[i] == "write" && i + 1 < toks.size() )
                        {
                            meta.State.DepthWrite = ( toks[i + 1] == "on" || toks[i + 1] == "true" );
                            ++i;
                        }
                        else if ( auto cmp = ParseCompare( toks[i] ) )
                        {
                            meta.State.DepthCompare = cmp;
                            meta.State.DepthTest    = true;
                        }
                    }
                }
                else if ( cmd == "topology" && toks.size() >= 2 )
                {
                    if ( toks[1] == "triangles" )
                        meta.State.Topology = StateTopology::Triangles;
                    else if ( toks[1] == "lines" )
                        meta.State.Topology = StateTopology::Lines;
                    else if ( toks[1] == "points" )
                        meta.State.Topology = StateTopology::Points;
                    else if ( toks[1] == "patches" )
                    {
                        meta.State.Topology = StateTopology::Patches;
                        if ( toks.size() >= 3 )
                        {
                            try
                            {
                                meta.State.PatchControlPoints = static_cast<uint32_t>( std::stoul( toks[2] ) );
                            }
                            catch ( ... )
                            {
                            }
                        }
                    }
                }
            }
        }

        return meta;
    }

} // namespace Desert::Core::Preprocess
