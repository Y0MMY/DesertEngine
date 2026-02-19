#include "ShaderIncluder.hpp"

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

        if ( !std::filesystem::exists( fullPath ) )
        {
            return CreateErrorIncludeResult( std::format( "Cannot open include file: {}", fullPath.string() ) );
        }

        std::ifstream file( fullPath );
        if ( !file.is_open() )
        {
            return CreateErrorIncludeResult( std::format( "Failed to open include file: {}", fullPath.string() ) );
        }

        std::string content( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );

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