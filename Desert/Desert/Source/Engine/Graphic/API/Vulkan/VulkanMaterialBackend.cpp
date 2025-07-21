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
    void VulkanMaterialBackend::ApplyPushConstants( MaterialExecutor* material, Pipeline* pipeline )
    {
    }

    void VulkanMaterialBackend::ApplyUniformBuffer( MaterialProperty* prop )
    {
        auto uniformProp = static_cast<UniformBufferProperty*>( prop );
        if ( !uniformProp || !uniformProp->IsDirty() )
            return;

        const auto& frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& vulkanShader = std::static_pointer_cast<VulkanShader>( m_Shader );

        if ( auto bufferInfo = uniformProp->GetUniform() )
        {
            if ( auto vulkanBuffer = sp_cast<Uniforms::API::Vulkan::VulkanUniformBuffer>( bufferInfo ) )
            {
                auto& bufferInfo = vulkanBuffer->GetDescriptorBufferInfo();
                auto  wds        = DescriptorSetBuilder::GetUniformWDS( vulkanShader, frameIndex, 0, // set 0
                                                                        vulkanBuffer->GetBinding(), 1U, &bufferInfo );

                m_PendingDescriptorWrites.push_back( wds );
                uniformProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::ApplyTexture2D( MaterialProperty* prop )
    {
        auto textureProp = static_cast<Texture2DProperty*>( prop );
        if ( !textureProp || !textureProp->IsDirty() )
            return;

        const auto& frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& vulkanShader = std::static_pointer_cast<VulkanShader>( m_Shader );

        if ( auto imageUniform = textureProp->GetUniform() )
        {
            if ( auto vulkanImage = sp_cast<Uniforms::API::Vulkan::VulkanUniformImage2D>( imageUniform ) )
            {
                auto& imageInfo = vulkanImage->GetDescriptorImageInfo();
                auto  wds       = DescriptorSetBuilder::GetSampler2DWDS( vulkanShader, frameIndex, 0, // set 0
                                                                         vulkanImage->GetBinding(), 1U, &imageInfo );

                m_PendingDescriptorWrites.push_back( wds );
                textureProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::ApplyTextureCube( MaterialProperty* prop )
    {
        auto textureProp = static_cast<TextureCubeProperty*>( prop );
        if ( !textureProp || !textureProp->IsDirty() )
            return;

        const auto& frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& vulkanShader = std::static_pointer_cast<VulkanShader>( m_Shader );

        if ( auto imageUniform = textureProp->GetUniform() )
        {
            if ( auto vulkanImage = sp_cast<Uniforms::API::Vulkan::VulkanUniformImageCube>( imageUniform ) )
            {
                auto& imageInfo = vulkanImage->GetDescriptorImageInfo();
                auto  wds       = DescriptorSetBuilder::GetSamplerCubeWDS( vulkanShader, frameIndex, 0, // set 0
                                                                           vulkanImage->GetBinding(), 1U, &imageInfo );

                m_PendingDescriptorWrites.push_back( wds );
                textureProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::FlushUpdates()
    {
        if ( !m_PendingDescriptorWrites.empty() )
        {
            const auto& rendererAPI =
                 static_cast<API::Vulkan::VulkanRendererAPI*>( Renderer::GetInstance().GetRendererAPI() );

            const auto& frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();
            const auto& vulkanShader = std::static_pointer_cast<VulkanShader>( m_Shader );

            rendererAPI->GetDescriptorManager()->UpdateDescriptorSet( vulkanShader, frameIndex, 0, // set 0
                                                                      m_PendingDescriptorWrites );
        }

        m_PendingDescriptorWrites.clear();
    }

} // namespace Desert::Graphic::API::Vulkan