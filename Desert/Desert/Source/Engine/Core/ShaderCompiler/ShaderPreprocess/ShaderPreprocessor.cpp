#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>

namespace Desert::Core::Preprocess::Shader
{
    namespace
    {
        enum class PragmaType
        {
            Stage = 0,
            Unknown
        };

        Formats::ShaderStage ParseShaderType( const std::string& typeStr )
        {
            if ( typeStr == "vertex" )
                return Formats::ShaderStage::Vertex;
            if ( typeStr == "fragment" )
                return Formats::ShaderStage::Fragment;

            return Formats::ShaderStage::None;
        }

        PragmaType GetPragmaType( std::string source )
        {
            source.erase( std::remove( source.begin(), source.end(), ' ' ), source.end() );

            if ( source == "stage" )
                return PragmaType::Stage;

            return PragmaType::Unknown;
        }

        PragmaType ParseLineWithPragma( const std::string& pragmaLine )
        {
            size_t pragmaTypeStart = pragmaLine.find( " " );
            if ( pragmaTypeStart != std::string::npos )
            {
                std::string pragmaTypeStr = pragmaLine.substr(
                     pragmaTypeStart + 1, pragmaLine.find( ":", pragmaTypeStart ) - pragmaTypeStart - 1 );
                return GetPragmaType( pragmaTypeStr );
            }

            return PragmaType::Unknown;
        }

        std::pair<Formats::ShaderStage, std::string>
        ExtractShaderStage( const std::string& pragmaLine, const std::string& source, size_t pragmaEnd )
        {
            size_t stageStart = pragmaLine.find( ":" );
            if ( stageStart != std::string::npos )
            {
                stageStart += 1;
                size_t shaderTypeStart = pragmaLine.find_first_not_of( " ", stageStart );

                if ( shaderTypeStart != std::string::npos )
                {
                    size_t shaderTypeEnd = pragmaLine.find_first_of( "\r\n", shaderTypeStart );
                    if ( shaderTypeEnd == std::string::npos )
                        shaderTypeEnd = pragmaLine.size();

                    const std::string shaderTypeStr =
                         pragmaLine.substr( shaderTypeStart, shaderTypeEnd - shaderTypeStart );
                    Formats::ShaderStage shaderType = ParseShaderType( shaderTypeStr );

                    size_t shaderStart = pragmaEnd + 1;
                    size_t nextPragma  = source.find( "#pragma stage", shaderStart );
                    size_t shaderEnd   = ( nextPragma != std::string::npos ) ? nextPragma : source.size();

                    std::string shaderBody = source.substr( shaderStart, shaderEnd - shaderStart );

                    return { shaderType, shaderBody };
                }
            }

            return { Formats::ShaderStage::None, "" };
        }
    } // namespace

    std::unordered_map<Formats::ShaderStage, std::string> PreProcess( const std::string& source )
    {
        std::unordered_map<Formats::ShaderStage, std::string> shaderMap;

        size_t startSearch = source.find( "#pragma" );
        while ( startSearch != std::string::npos )
        {
            size_t pragmaEnd = source.find_first_of( "\r\n", startSearch );
            if ( pragmaEnd == std::string::npos )
            {
                pragmaEnd = source.size();
            }

            const std::string pragmaLine = source.substr( startSearch, pragmaEnd - startSearch );

            PragmaType pragmaType = ParseLineWithPragma( pragmaLine );

            if ( pragmaType == PragmaType::Stage )
            {
                auto [shaderType, shaderBody] = ExtractShaderStage( pragmaLine, source, pragmaEnd );
                shaderMap[shaderType]         = shaderBody;
            }
            startSearch = source.find( "#pragma", pragmaEnd );
        }
        return shaderMap;
    }

} // namespace Desert::Core::Preprocess::Shader
