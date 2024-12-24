#pragma once

#include <Engine/Graphic/Shader.hpp>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanShader final : public Shader
    {
    public:
        VulkanShader( const std::filesystem::path& path );

        virtual void Use( BindUsage use = BindUsage::Bind ) const override
        {
        }
        virtual void RT_Use( BindUsage use = BindUsage::Bind ) const override
        {
        }
        virtual Common::BoolResult Reload() override;
        virtual const std::string  GetName() const override
        {
            return "";
        }

        const std::vector<VkPipelineShaderStageCreateInfo>& GetPipelineShaderStageCreateInfos() const
        {
            return m_PipelineShaderStageCreateInfos;
        }

    private:
        std::vector<VkPipelineShaderStageCreateInfo> m_PipelineShaderStageCreateInfos;
        std::filesystem::path                        m_ShaderPath;
    };

} // namespace Desert::Graphic::API::Vulkan