#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShaderResource.hpp>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanShader final : public Shader
    {
    public:
        struct DescriptorSetInfo
        {
            VkDescriptorPool             Pool;
            std::vector<VkDescriptorSet> DescriptorSets;
        };

        struct ReflectionData
        {
            std::vector<ShaderResource::ShaderDescriptorSet> ShaderDescriptorSets;
        };

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
            return "TODO";
        }

        const VkWriteDescriptorSet GetDescriptorSet( const std::string& name, uint32_t set ) const;

        const std::vector<VkPipelineShaderStageCreateInfo>& GetPipelineShaderStageCreateInfos() const
        {
            return m_PipelineShaderStageCreateInfos;
        }
        std::vector<VkDescriptorSetLayout> GetAllDescriptorSetLayouts();
        DescriptorSetInfo                  CreateDescriptorSets( uint32_t numberOfSets );

        auto& GetShaderDescriptorSets()
        {
            return m_ReflectionData.ShaderDescriptorSets;
        }

    private:
        void               Reflect( VkShaderStageFlagBits flag, const std::vector<uint32_t>& spirvBinary );
        Common::BoolResult CreateDescriptorsLayout();
        Common::BoolResult CreateDescriptors();

    private:
        std::vector<VkPipelineShaderStageCreateInfo> m_PipelineShaderStageCreateInfos;
        std::filesystem::path                        m_ShaderPath;

        ReflectionData                     m_ReflectionData;
        std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;
    };

} // namespace Desert::Graphic::API::Vulkan