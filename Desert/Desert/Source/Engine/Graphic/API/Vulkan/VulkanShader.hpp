#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShaderResource.hpp>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    enum class WriteDescriptorType
    {
        Uniform = 0,
        Sampler2D,
        SamplerCube,
        StorageImage
    };

    class VulkanShader final : public Shader
    {
    public:
        struct DescriptorSetInfo
        {
            std::vector<VkDescriptorPool>             Pool;
            std::vector<std::vector<VkDescriptorSet>> DescriptorSets; // frame -> set
        };

        struct ReflectionData
        {
            std::unordered_map<SetPoint, ShaderResource::ShaderDescriptorSet>
                 ShaderDescriptorSets; // SetPoint = set

            std::optional<Core::Models::PushConstant> PushConstantRanges;
        };

    public:
        VulkanShader( const std::filesystem::path& path, const ShaderDefines& defines );

        virtual void Use( BindUsage use = BindUsage::Bind ) const override
        {
        }
        virtual void RT_Use( BindUsage use = BindUsage::Bind ) const override
        {
        }
        virtual Common::BoolResult Reload() override;
        virtual const std::string  GetName() const override
        {
            return m_ShaderName;
        }

        virtual const std::vector<Core::Models::UniformBuffer>
        GetUniformBufferModels() const override; // don't use it often! TODO: cache
        virtual const std::vector<Core::Models::ImageCubeSampler> GetUniformImageCubeModels() const override;
        virtual const std::vector<Core::Models::Image2DSampler>   GetUniformImage2DModels() const override;

        const std::vector<VkPipelineShaderStageCreateInfo>& GetPipelineShaderStageCreateInfos() const
        {
            return m_PipelineShaderStageCreateInfos;
        }
        std::vector<VkDescriptorSetLayout> GetAllDescriptorSetLayouts();
        void                               CreateDescriptorSets( uint32_t framesInFlight );
        DescriptorSetInfo                  AllocateDescriptorSets( uint32_t framesInFlight );

        auto& GetShaderDescriptorSets()
        {
            return m_ReflectionData.ShaderDescriptorSets;
        }

        auto& GetShaderPushConstant()
        {
            return m_ReflectionData.PushConstantRanges;
        }

        auto& GetVulkanDescriptorSetInfo() const
        {
            return m_DescriptorSetInfo;
        }

    private:
        void               Reflect( VkShaderStageFlagBits flag, const std::vector<uint32_t>& spirvBinary );
        Common::BoolResult CreateDescriptorsLayout();
        Common::BoolResult CreateDescriptors();

    private:
        std::vector<VkPipelineShaderStageCreateInfo> m_PipelineShaderStageCreateInfos;
        std::filesystem::path                        m_ShaderPath;
        std::string                                  m_ShaderName;

        ReflectionData                     m_ReflectionData;
        std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts; // set

        DescriptorSetInfo m_DescriptorSetInfo;
    };

} // namespace Desert::Graphic::API::Vulkan