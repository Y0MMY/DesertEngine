#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>

#include <Engine/Uniforms/API/Vulkan/VulkanUniformBuffer.hpp>
#include <Engine/Uniforms/API/Vulkan/VulkanUniformImage2D.hpp>
#include <Engine/Uniforms/API/Vulkan/VulkanUniformImageCube.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>

namespace Desert::Graphic::API::Vulkan
{
    void VulkanMaterialBackend::ApplyProperties( Material* material )
    {
        if ( !material )
            return;

        uint32_t    frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& vulkanShader = std::static_pointer_cast<VulkanShader>( material->GetShader() );

        std::vector<VkWriteDescriptorSet> writeDescriptorSets;

        // Process uniform buffers
        for ( const auto& [name, prop] : material->GetUniformBufferProperties() )
        {
            const auto& bufferInfo = prop->GetUniform();
            auto        vulkanBufferInfo =
                 sp_cast<Uniforms::API::Vulkan::VulkanUniformBuffer>( bufferInfo )->GetDescriptorBufferInfo();

            auto wds = DescriptorSetBuilder::GetUniformWDS( vulkanShader, frameIndex,
                                                            0, // set 0
                                                            bufferInfo->GetBinding(), 1U, &vulkanBufferInfo );
            writeDescriptorSets.push_back( wds );
        }

        // Process 2D textures
        for ( const auto& [name, prop] : material->GetTexture2DProperties() )
        {
            const auto& imageInfo = sp_cast<Uniforms::API::Vulkan::VulkanUniformImage2D>( prop->GetUniform() );

            auto wds = DescriptorSetBuilder::GetSamplerWDS( vulkanShader, frameIndex,
                                                            0, // set 0
                                                            imageInfo->GetBinding(), 1U,
                                                            &imageInfo->GetDescriptorImageInfo() );
            writeDescriptorSets.push_back( wds );
        }

        // Process cube  textures
        for ( const auto& [name, prop] : material->GetTextureCubeProperties() )
        {
            const auto& imageInfo = sp_cast<Uniforms::API::Vulkan::VulkanUniformImageCube>( prop->GetUniform() );

            auto wds = DescriptorSetBuilder::GetSamplerWDS( vulkanShader, frameIndex,
                                                            0, // set 0
                                                            imageInfo->GetBinding(), 1U,
                                                            &imageInfo->GetDescriptorImageInfo() );
            writeDescriptorSets.push_back( wds );
        }

        if ( !writeDescriptorSets.empty() )
        {
            static_cast<API::Vulkan::VulkanRendererAPI*>( Renderer::GetInstance().GetRendererAPI() )
                 ->GetDescriptorManager()
                 ->UpdateDescriptorSet( vulkanShader, frameIndex, 0, writeDescriptorSets );
        }
    }

    void VulkanMaterialBackend::ApplyPushConstants( Material* material, Pipeline* pipeline )
    {
    }

    void VulkanMaterialBackend::ApplyUniformBuffer( MaterialProperty* prop )
    {
    }

    void VulkanMaterialBackend::ApplyTexture2D( MaterialProperty* prop )
    {
    }

    void VulkanMaterialBackend::ApplyTextureCube( MaterialProperty* prop )
    {
    }

} // namespace Desert::Graphic::API::Vulkan