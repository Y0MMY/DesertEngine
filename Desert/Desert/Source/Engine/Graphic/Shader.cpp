#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/Graphic/RendererAPI.hpp>

namespace Desert::Graphic
{

    static std::string MakeKey( const std::string& name, const ShaderDefines& defines )
    {
        std::string key = name;
        for ( const auto& [define, value] : defines )
        {
            key += "_" + define + "_" + value;
        }
        return key;
    }

    std::shared_ptr<Shader> Shader::Create( const std::string& filename, const ShaderDefines& defines )
    {

        const auto shaderKey   = MakeKey( filename, defines );
        const bool shaderFound = ShaderLibrary::IsShaderInLibrary( filename );
        if ( shaderFound )
        {
            return ShaderLibrary::Get( filename, defines ).GetValue();
        }
        std::shared_ptr<Shader> shader = nullptr;
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {
                shader = std::make_shared<API::Vulkan::VulkanShader>(
                     Common::Constants::Path::SHADERDIR_PATH / filename, defines );
            }
        }

        DESERT_VERIFY( shader );
        ShaderLibrary::LoadShader( shader, defines );
        return shader;
    }

    void ShaderLibrary::LoadShader( const std::shared_ptr<Shader>& shader, const ShaderDefines& defines )
    {
        const std::string shaderName = shader->GetName();

        const auto shaderKey = MakeKey( shaderName, defines );
        if ( IsShaderInLibrary( shaderKey ) )
        {
            LOG_ERROR( "A shader named {} was FOUND in the library, no loading was performed", shaderName );
            return;
        }

        s_AllShaders[shaderKey] = shader;
        LOG_TRACE( "A shader named {} was NOT FOUND in the library, loading was performed", shaderName );
        for ( const auto& [define, value] : defines )
        {
            LOG_TRACE( "\tDefine: {}, Key: {}", define, value );
        }
    }

    Common::Result<std::shared_ptr<Shader>> ShaderLibrary::Get( const std::string&   shaderName,
                                                                const ShaderDefines& defines )
    {
        const auto shaderKey = MakeKey( shaderName, defines );
        if ( !IsShaderInLibrary( shaderKey ) )
        {
            return Common::MakeFormattedError<std::shared_ptr<Shader>>(
                 "A shader named {} was NOT FOUND in the library", shaderName );
        }

        return Common::MakeSuccess( s_AllShaders[shaderKey] );
    }

    bool ShaderLibrary::IsShaderInLibrary( const std::string& shaderName )
    {
        return s_AllShaders.find( shaderName ) != s_AllShaders.end();
    }

} // namespace Desert::Graphic