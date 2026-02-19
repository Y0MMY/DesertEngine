#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>

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

            return Formats::ShaderStage::None;
        }

    } // namespace

    std::unordered_map<Desert::Core::Formats::ShaderStage, std::string>
    ShaderPreprocess::PreProcessProgram( const std::string& source, const std::filesystem::path& basePath )
    {
        using namespace Desert::Core::Formats;

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

            DESERT_VERIFY( std::filesystem::exists( stagePath ), "Shader stage file not found: {}",
                           stagePath.string() );

            std::ifstream file( stagePath );
            DESERT_VERIFY( file.is_open(), "Failed to open shader stage file: {}", stagePath.string() );

            std::string stageSource( ( std::istreambuf_iterator<char>( file ) ),
                                     std::istreambuf_iterator<char>() );

            DESERT_VERIFY( programStages.find( stage ) == programStages.end(),
                           "Duplicate shader stage in program: {}", Shader::GetStringShaderStage( stage ) );

            programStages.emplace( stage, std::move( stageSource ) );
        }

        DESERT_VERIFY( !programStages.empty(), "Shader program contains no stages" );

        return programStages;
    }

} // namespace Desert::Core::Preprocess
