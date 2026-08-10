#pragma once

#include <Engine/Graphic/Materials/MaterialBackend.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanMaterialBackend final : public MaterialBackend
    {
    public:
        VulkanMaterialBackend( const std::shared_ptr<Shader>& shader );
        ~VulkanMaterialBackend();

        virtual void InitializeDefaults() override;

        virtual void ApplyUniformBuffer( MaterialProperty* prop ) override;
        virtual void ApplyStorageBuffer( MaterialProperty* prop ) override;
        virtual void ApplyTexture2D( MaterialProperty* prop ) override;
        virtual void ApplyTextureCube( MaterialProperty* prop ) override;

        virtual void FlushUpdates() override;

        virtual void ApplyPushConstants( MaterialExecutor* material, GraphicsPipeline* pipeline ) override;

        VkDescriptorSet GetDescriptorSet( uint32_t frameIndex, uint32_t setIndex = 0 ) const;

        void UpdateDescriptorSets( const std::vector<VkWriteDescriptorSet>& writes, bool force = false );

        void BindDescriptorSets( VkCommandBuffer cmdBuffer, VkPipelineLayout layout, VkPipelineBindPoint bindPoint,
                                 uint32_t frameIndex );

        void ResetFrameUpdateState( uint32_t frameIndex );
        bool HasDescriptorSets() const;

        /** The layouts this backend's descriptor sets were allocated from, captured at construction. */
        const std::vector<DescriptorSetLayoutRef>& GetLayouts() const
        {
            return m_Layouts;
        }

        /** The shader's reload generation at the moment those sets were allocated. */
        uint32_t GetShaderGeneration() const
        {
            return m_ShaderGeneration;
        }

    private:
        void InitializeWithFallbacks();

        void AllocateDescriptorSets();
        void CreateDescriptorPool();

        // Says so, once, if the shader has been recompiled into a DIFFERENT SHAPE since these sets were
        // allocated. Silent when the recompile kept the same bindings, because that case is genuinely
        // fine — Vulkan compares set layouts by content.
        void ReportShapeDriftOnce();

        std::shared_ptr<VulkanShader> m_VulkanShader; // TODO: weak ptr
        VkDescriptorPool              m_DescriptorPool = VK_NULL_HANDLE;

        // The layouts every set below was allocated from, held for as long as those sets exist. Not
        // re-read from the shader: a recompile publishes new layouts, and a set allocated from the old
        // one has to keep the old one alive to stay legal.
        std::vector<DescriptorSetLayoutRef> m_Layouts;
        uint32_t                            m_ShaderGeneration   = 0;
        bool                                m_ShapeDriftReported = false;

        // [frame][renderer slot][set].
        //
        // The SLOT dimension is what stops two views from sharing one set: the descriptor a draw uses is
        // looked up with the slot of the renderer that is recording (EngineContext::GetActiveRendererSlot),
        // so a second SceneRenderer writes and binds its own copies instead of the first one's. Every read
        // and write goes through GetDescriptorSet, which resolves the slot in one place.
        std::vector<std::vector<std::vector<VkDescriptorSet>>> m_DescriptorSets;

        // Track updates per [frame][slot][set] (absolute frame count). Per SLOT as well, or a slot that
        // was not active when the frame's updates ran would keep whatever its set held last — the guard
        // would report the work as already done for a set nobody wrote.
        std::vector<std::vector<std::vector<uint64_t>>> m_DescriptorSetsUpdateFrame;

        VkBuffer      m_DummyBuffer = VK_NULL_HANDLE;
        VmaAllocation m_DummyAllocation = nullptr;
    };
} // namespace Desert::Graphic::API::Vulkan