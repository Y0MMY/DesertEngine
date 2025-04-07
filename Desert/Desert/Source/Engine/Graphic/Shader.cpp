#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/Graphic/RendererAPI.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<Shader> Shader::Create( const std::string& filename )
    {
        const bool shaderFound = ShaderLibrary::IsShaderInLibrary( filename );
        if ( shaderFound )
        {
            return ShaderLibrary::Get( filename ).GetValue();
        }
        std::shared_ptr<Shader> shader = nullptr;
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {
                shader = std::make_shared<API::Vulkan::VulkanShader>( Common::Constants::Path::SHADERDIR_PATH /
                                                                      filename );
            }
        }

        DESERT_VERIFY( shader );
        ShaderLibrary::LoadShader( shader );
        return shader;
    }

    void ShaderLibrary::LoadShader( const std::shared_ptr<Shader>& shader )
    {
        const std::string shaderName = shader->GetName();
        if ( IsShaderInLibrary( shaderName ) )
        {
            LOG_ERROR( "A shader named {} was FOUND in the library, no loading was performed", shaderName );
            return;
        }

        s_AllShaders[shaderName] = shader;
        LOG_TRACE( "A shader named {} was NOT FOUND in the library, loading was performed", shaderName );
    }

    Common::Result<std::shared_ptr<Shader>> ShaderLibrary::Get( const std::string& shaderName )
    {
        if ( !IsShaderInLibrary( shaderName ) )
        {
            LOG_ERROR( "A shader named {} was not FOUND in the library", shaderName );
            return Common::MakeFormattedError<std::shared_ptr<Shader>>(
                 "A shader named {} was not FOUND in the library", shaderName );
        }

        return Common::MakeSuccess( s_AllShaders[shaderName] );
    }

    bool ShaderLibrary::IsShaderInLibrary( const std::string& shaderName )
    {
        return s_AllShaders.find( shaderName ) != s_AllShaders.end();
    }

} // namespace Desert::Graphic