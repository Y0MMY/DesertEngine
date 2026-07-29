#include "ShaderIncluder.hpp"

#include <Common/Utilities/FileSystem.hpp>
#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>

namespace Desert::Core
{

    ShaderIncluder::ShaderIncluder( const Common::Filepath& basePath ) : m_BasePath( basePath )
    {
    }

    shaderc_include_result* ShaderIncluder::GetInclude( const char* requested_source, shaderc_include_type type,
                                                        const char* requesting_source, size_t include_depth )
    {
        DESERT_VERIFY( include_depth < 32, "Shader include recursion detected" );

        std::filesystem::path fullPath;

        if ( type == shaderc_include_type_relative )
        {
            // #include "file"
            std::filesystem::path baseDir = std::filesystem::path( requesting_source ).parent_path();

            fullPath = ( baseDir / requested_source ).lexically_normal();
        }
        else
        {
            // #include <file>
            std::filesystem::path shaderRoot = Common::Constants::Path::SHADERDIR_PATH;

            fullPath = ( shaderRoot / requested_source ).lexically_normal();
        }

        // FileSystem is VFS-aware: shader includes resolve from disk in dev and from the mounted
        // .dpak in a packaged game.
        if ( !Common::Utils::FileSystem::Exists( fullPath ) )
        {
            return CreateErrorIncludeResult( std::format( "Cannot open include file: {}", fullPath.string() ) );
        }

        // Translate the Desert layout sugar so shared `.glslh` headers can use the SAME vocabulary as the
        // stage blocks (the compiler inlines includes AFTER stage assembly, so headers must be translated
        // here). Line-preserving, so #line-based include error mapping stays exact.
        std::string content =
             Preprocess::DShaderParser::TranslateSugar( Common::Utils::FileSystem::ReadFileContent( fullPath ) );

        auto result = new shaderc_include_result;

        auto sourceName = new std::string( fullPath.string() );
        auto contentStr = new std::string( std::move( content ) );

        result->source_name        = sourceName->c_str();
        result->source_name_length = sourceName->length();
        result->content            = contentStr->c_str();
        result->content_length     = contentStr->length();

        result->user_data =
             new std::pair<std::string, std::string>( std::move( *sourceName ), std::move( *contentStr ) );

        return result;
    }

    shaderc_include_result* ShaderIncluder::CreateErrorIncludeResult( const std::string& error )
    {
        auto result    = new shaderc_include_result;
        auto error_str = new std::string( error );

        result->source_name        = "";
        result->source_name_length = 0;
        result->content            = error_str->c_str();
        result->content_length     = error_str->length();
        result->user_data          = new std::pair<std::string, std::string>( "", *error_str );

        return result;
    }

    void ShaderIncluder::ReleaseInclude( shaderc_include_result* data )
    {
        if ( data && data->user_data )
        {
            auto userData = static_cast<std::pair<std::string, std::string>*>( data->user_data );
            delete userData;
            delete data;
        }
    }
} // namespace Desert::Core