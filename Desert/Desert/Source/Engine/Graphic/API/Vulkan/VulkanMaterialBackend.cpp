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
        for ( const auto& [name, index] : material->GetUniformBufferProperties() )
        {
            if ( auto property = material->GetUniformBufferProperty( name ) )
            {
                if ( auto bufferInfo = property->GetUniform() )
                {
                    if ( auto vulkanBuffer = sp_cast<Uniforms::API::Vulkan::VulkanUniformBuffer>( bufferInfo ) )
                    {
                        auto& bufferInfo = vulkanBuffer->GetDescriptorBufferInfo();
                        auto  wds =
                             DescriptorSetBuilder::GetUniformWDS( vulkanShader, frameIndex,
                                                                  0, // set 0
                                                                  vulkanBuffer->GetBinding(), 1U, &bufferInfo );
                        writeDescriptorSets.push_back( wds );
                    }
                }
            }
        }

        // Process 2D textures
        for ( const auto& [name, index] : material->GetTexture2DProperties() )
        {
            if ( auto property = material->GetTexture2DProperty( name ) )
            {
                if ( auto imageUniform = property->GetUniform() )
                {
                    if ( auto vulkanImage = sp_cast<Uniforms::API::Vulkan::VulkanUniformImage2D>( imageUniform ) )
                    {
                        auto& imageInfo = vulkanImage->GetDescriptorImageInfo();
                        auto  wds =
                             DescriptorSetBuilder::GetSamplerWDS( vulkanShader, frameIndex,
                                                                  0, // set 0
                                                                  vulkanImage->GetBinding(), 1U, &imageInfo );
                        writeDescriptorSets.push_back( wds );
                    }
                }
            }
        }

        // Process cube textures
        for ( const auto& [name, index] : material->GetTextureCubeProperties() )
        {
            if ( auto property = material->GetTextureCubeProperty( name ) )
            {
                if ( auto imageUniform = property->GetUniform() )
                {
                    if ( auto vulkanImage =
                              sp_cast<Uniforms::API::Vulkan::VulkanUniformImageCube>( imageUniform ) )
                    {
                        auto& imageInfo = vulkanImage->GetDescriptorImageInfo();
                        auto  wds =
                             DescriptorSetBuilder::GetSamplerWDS( vulkanShader, frameIndex,
                                                                  0, // set 0
                                                                  vulkanImage->GetBinding(), 1U, &imageInfo );
                        writeDescriptorSets.push_back( wds );
                    }
                }
            }
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