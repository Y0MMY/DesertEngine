#include "ShaderIncluder.hpp"

namespace Desert::Core
{

    ShaderIncluder::ShaderIncluder( const Common::Filepath& basePath ) : m_BasePath( basePath )
    {
    }

    shaderc_include_result* ShaderIncluder::GetInclude( const char* requested_source, shaderc_include_type type,
                                                        const char* requesting_source, size_t include_depth )
    {
        std::string fullPath;
        if ( type == shaderc_include_type_relative )
        {
            std::filesystem::path baseDir = std::filesystem::path( requesting_source ).parent_path();
            fullPath                      = ( baseDir / requested_source ).string();
        }
        else
        {
            fullPath = requested_source;
        }

        std::ifstream file( fullPath );
        if ( !file.is_open() )
        {
            return CreateErrorIncludeResult( std::format( "Cannot open include file: {}", fullPath ) );
        }

        std::string content( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );

        auto result      = new shaderc_include_result;
        auto source_name = new std::string( fullPath );
        auto content_str = new std::string( content );

        result->source_name        = source_name->c_str();
        result->source_name_length = source_name->length();
        result->content            = content_str->c_str();
        result->content_length     = content_str->length();
        result->user_data =
             new std::pair<std::string, std::string>( std::move( *source_name ), std::move( *content_str ) );

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
        result->user_data          = error_str;

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