#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDescriptorSetLayout.hpp>
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

    public:
        VulkanShader( const Assets::Asset<Assets::ShaderAsset>& asset, const ShaderDefines& defines,
                      const std::string& passName = {} );
        ~VulkanShader();

        virtual void Use( BindUsage use = BindUsage::Bind ) const override
        {
        }
        virtual void RT_Use( BindUsage use = BindUsage::Bind ) const override
        {
        }
        virtual Common::BoolResultStr Reload() override;
        virtual const std::string     GetName() const override
        {
            return m_ShaderName;
        }

        virtual const std::vector<ShaderResources::ShaderLayout::UniformBuffer>
        GetUniformBufferModels() const override; // don't use it often! TODO: cache
        virtual const std::vector<ShaderResources::ShaderLayout::StorageBuffer>
        GetStorageBufferModels() const override; // don't use it often! TODO: cache
        virtual const std::vector<ShaderResources::ShaderLayout::ImageCubeSampler> GetUniformImageCubeModels() const override;
        virtual const std::vector<ShaderResources::ShaderLayout::Image2DSampler>   GetUniformImage2DModels() const override;

        virtual const Common::Filepath& GetFilepath() const override
        {
            return m_ShaderPath;
        }

        virtual const Core::Formats::ShaderProgramMeta& GetProgramMeta() const override
        {
            return m_ProgramMeta;
        }

        const std::vector<VkPipelineShaderStageCreateInfo>& GetPipelineShaderStageCreateInfos() const
        {
            return m_PipelineShaderStageCreateInfos;
        }

        // No stages means CompileProgram never succeeded — it is transactional, so a shader that has ever
        // compiled keeps its modules even if a later recompile fails. Reading the stage list rather than
        // a separate bool keeps this from becoming a second piece of state that can disagree with the
        // first: the list IS what a pipeline would be built from.
        [[nodiscard]] virtual bool IsCompiled() const override
        {
            return !m_PipelineShaderStageCreateInfos.empty();
        }
        /**
         * The layout for @p set, as a STRONG reference.
         *
         * Every caller is expected to keep it for as long as it keeps whatever it builds from it — a
         * pipeline layout, a descriptor pool, an allocated set. A recompile replaces this shader's
         * references; it does not reach into objects that are still standing on the old ones. See
         * VulkanDescriptorSetLayout.hpp for the failure this arrangement exists to make impossible.
         */
        DescriptorSetLayoutRef GetDescriptorSetLayout( uint32_t set ) const
        {
            return set < m_DescriptorSetLayouts.size() ? m_DescriptorSetLayouts[set] : nullptr;
        }

        const auto GetDescriptorSetLayoutCount() const
        {
            return m_DescriptorSetLayouts.size();
        }

        const std::vector<DescriptorSetLayoutRef>& GetAllDescriptorSetLayouts() const
        {
            return m_DescriptorSetLayouts;
        }

        /**
         * Bumped by every successful recompile.
         *
         * What it is for: an object built from this shader records the generation it was built at, and
         * a later mismatch means "you are running code this shader no longer contains". That is a
         * legitimate state — a hot reload cannot reach a pipeline the renderer built and owns — but it
         * is never a silent one.
         */
        uint32_t GetReloadGeneration() const
        {
            return m_ReloadGeneration;
        }

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

        virtual const ShaderDefines& GetDefines() const override
        {
            return {};
        }

    private:
        // Fails when a stage declares an image resource the engine cannot bind: reflection refuses to
        // register it, so the descriptor layout would silently lack the binding. Better to lose the
        // shader with a named reason than to bind something of the wrong shape.
        Common::BoolResultStr Reflect( VkShaderStageFlagBits flag, const std::vector<uint32_t>& spirvBinary,
                                       ShaderResource::ReflectionData& into );
        Common::BoolResultStr CreateDescriptorsLayout();

        Common::BoolResultStr
        CompileProgram( const std::unordered_map<Core::Formats::ShaderStage, std::string>& stages );

    private:
        const std::weak_ptr<Assets::ShaderAsset> m_ShaderAsset;

    private:
        std::vector<VkPipelineShaderStageCreateInfo> m_PipelineShaderStageCreateInfos;
        std::vector<VkShaderModule>                  m_ShaderModules;
        std::filesystem::path                        m_ShaderPath;
        std::string                                  m_ShaderName;
        std::string                                  m_PassName; // empty = default program

        Core::Formats::ShaderProgramMeta             m_ProgramMeta;

        ShaderResource::ReflectionData      m_ReflectionData;
        std::vector<DescriptorSetLayoutRef> m_DescriptorSetLayouts; // indexed by set

        // Monotonic; 0 means "never compiled". Never reset, so a comparison against a recorded value
        // stays meaningful for the life of the process.
        uint32_t m_ReloadGeneration = 0;

        DescriptorSetInfo m_DescriptorSetInfo;
    };

} // namespace Desert::Graphic::API::Vulkan