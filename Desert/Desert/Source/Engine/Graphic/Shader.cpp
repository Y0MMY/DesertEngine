#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/Graphic/RendererAPI.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<Shader> Shader::Create( const Assets::Asset<Assets::ShaderAsset>& asset,
                                            const ShaderDefines& defines, const std::string& passName )
    {
        std::shared_ptr<Shader> shader = nullptr;
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {
                shader = std::make_shared<API::Vulkan::VulkanShader>( asset, defines, passName );
            }
        }

        DESERT_VERIFY( shader );
        return shader;
    }

} // namespace Desert::Graphic