#pragma once

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanPipelineCompute final : public ComputePipeline
    {
    public:
        VulkanPipelineCompute( const ComputePipelineSpecification& spec );
        ~VulkanPipelineCompute() override;

        [[nodiscard]] virtual PipelineType GetType() const override { return PipelineType::Compute; }
        [[nodiscard]] virtual const std::shared_ptr<Shader>& GetShader() const override { return m_Specification.Shader; }

        [[nodiscard]] virtual const ComputePipelineSpecification& GetSpecification() const override
        {
            return m_Specification;
        }

        // --- Resource-binding API (see ComputePipeline) ---
        ComputePipeline& SetInput( uint32_t binding, Image* image ) override;
        ComputePipeline& SetOutput( uint32_t binding, Image* image, uint32_t mip = 0 ) override;
        ComputePipeline& SetStorageBuffer( uint32_t binding, ShaderResources::StorageBuffer* buffer ) override;
        ComputePipeline& SetPushConstants( const void* data, uint32_t size ) override;
        void             Dispatch( uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ ) override;

        // In-frame dispatch: records bind + descriptors + dispatch into an EXISTING (frame) command
        // buffer, outside any render pass. Unlike the immediate Dispatch() it neither submits nor
        // transitions image layouts — the caller owns layout transitions and inter-dispatch barriers
        // (see Renderer::DispatchComputeInFrame). Each call consumes a fresh descriptor set from an
        // internal ring so several dispatches recorded into one command buffer don't alias a single
        // set (a descriptor set's contents are consumed at execution time, not at record time).
        void RecordInFrame( VkCommandBuffer cmd, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ );

        [[nodiscard]] Image* GetInput( uint32_t binding ) const override;
        [[nodiscard]] Image* GetOutput( uint32_t binding ) const override;

        virtual void Invalidate() override;
        virtual void Release() override;

        const VkPipeline GetVkPipeline() const
        {
            return m_ComputePipeline;
        }

        const VkPipelineLayout GetVkPipelineLayout() const
        {
            return m_ComputePipelineLayout;
        }

        const auto GetCommandBuffer() const
        {
            return m_ActiveComputeCommandBuffer;
        }

        void BindDescriptorSets( VkDescriptorSet descriptorSet, uint32_t frameIndex );
        void UpdateDescriptorSet( uint32_t frameIndex, const std::vector<VkWriteDescriptorSet>& writes,
                                  VkDescriptorSet descriptorSet, uint32_t setIndex = 0 );

        VulkanMaterialBackend* GetVulkanMaterialBackend() const
        {
            return m_VulkanMaterialBackend.get();
        }

    private:
        ComputePipelineSpecification m_Specification;
        VkPipeline                   m_ComputePipeline       = VK_NULL_HANDLE;
        VkPipelineLayout             m_ComputePipelineLayout = VK_NULL_HANDLE;
        VkPipelineCache              m_PipelineCache         = VK_NULL_HANDLE;

        std::unique_ptr<VulkanMaterialBackend> m_VulkanMaterialBackend;

        VkCommandBuffer m_ActiveComputeCommandBuffer = nullptr;

        // Bound resources for the next Dispatch (set via SetInput/SetOutput/SetPushConstants).
        struct OutputBinding
        {
            Image*   Image = nullptr;
            uint32_t Mip   = 0;
        };
        std::unordered_map<uint32_t, Image*>                          m_BoundInputs;
        std::unordered_map<uint32_t, OutputBinding>                   m_BoundOutputs;
        std::unordered_map<uint32_t, ShaderResources::StorageBuffer*> m_BoundStorageBuffers;
        std::vector<std::byte>                                        m_BoundPushConstants;

        // Records the currently-bound resources into @p cmd against @p descriptorSet and dispatches.
        // Shared by the immediate Dispatch() and the in-frame RecordInFrame(); performs no layout
        // transitions and no submission of its own.
        void RecordDescriptorsAndDispatch( VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
                                           uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ );

        // Lazily-created ring of descriptor sets for in-frame dispatches (one per dispatch so they
        // don't alias). Sized generously across frames-in-flight; reused round-robin.
        void                         EnsureInFrameRing();
        VkDescriptorPool             m_InFramePool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_InFrameRing;
        uint32_t                     m_InFrameCursor = 0;
        static constexpr uint32_t    kInFrameRingSize = 64;
    };
} // namespace Desert::Graphic::API::Vulkan