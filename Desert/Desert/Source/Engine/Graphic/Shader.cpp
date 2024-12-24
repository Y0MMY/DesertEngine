#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/Graphic/RendererAPI.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<Shader> Shader::Create( const std::string& filename )
    {
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
        // s_AllShaders.push_back( shader );
        return shader;
    }
} // namespace Desert::Graphic