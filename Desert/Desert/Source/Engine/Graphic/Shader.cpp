#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/Graphic/RendererAPI.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<Shader> Shader::Create( const Assets::Asset<Assets::ShaderAsset>& asset,
                                            const ShaderDefines&                      defines )
    {
        std::shared_ptr<Shader> shader = nullptr;
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {
                shader = std::make_shared<API::Vulkan::VulkanShader>( asset, defines );
            }
        }

        DESERT_VERIFY( shader );
        return shader;
    }

    std::string Shader::GetStringShaderStage( const Core::Formats::ShaderStage stage )
    {
        switch ( stage )
        {
            case Core::Formats::ShaderStage::Fragment:
                return "Fragment";
            case Core::Formats::ShaderStage::Vertex:
                return "Vertex";
            case Core::Formats::ShaderStage::Compute:
                return "Compute";
        }

        return "Unknown";
    }

} // namespace Desert::Graphic