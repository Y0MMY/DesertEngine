#include <Engine/Graphic/API/Vulkan/VulkanMaterial.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>

namespace Desert::Graphic::API::Vulkan
{

    // NOTE: deprecated vvvvvvvvv

    /*

    +---------------------------------------------------------------------------------+
    |                     std::vector<uint8_t> |  (Buffer binary data)                |
    +---------------------------------------------------------------------------------+
                                          |
                                          v
    +---------------------------------------------------------------------------------+
    | std::unordered_map<std::string,std::pair<uint32_t, Core::Models::UniformBuffer>>|
    |                                                                                 |
    | - first: std::string (uniform name)                                             |
    | - second: std::pair<uint32_t, uint32_t>                                         |
    |   - first: uint32_t (buffer offset)                                             |
    |   - second: Core::Models::UniformBuffer (uniform buffer info)                   |
    +---------------------------------------------------------------------------------+

    */

    static constexpr uint32_t kMaxPushConstantsSize = 128U;

    VulkanMaterial::VulkanMaterial( const std::string& debugName, const std::shared_ptr<Shader>& shader )
         : m_Shader( shader ), m_DebugName( debugName )
    {
        m_PushConstantBuffer.Allocate( kMaxPushConstantsSize );
    }

    Common::BoolResult VulkanMaterial::Invalidate()
    {
        if ( !m_Shader )
        {
            return Common::MakeFormattedError( "{}: shader was not attached", m_DebugName );
        }

        Clear();
        return BOOLSUCCESS;
    }

    Common::BoolResult
    VulkanMaterial::AddUniformBufferToOverride( const std::shared_ptr<Uniforms::UniformBuffer>& uniformBuffer )
    {
        m_OverriddenUniforms.Buffers.push_back(
             sp_cast<Uniforms::API::Vulkan::VulkanUniformBuffer>( uniformBuffer ) );
        return BOOLSUCCESS;
    }

    Common::BoolResult
    VulkanMaterial::AddUniformCubeToOverride( const std::shared_ptr<Uniforms::UniformImageCube>& uniformCube )
    {
        m_OverriddenUniforms.ImageCubes.push_back(
             sp_cast<Uniforms::API::Vulkan::VulkanUniformImageCube>( uniformCube ) );
        return BOOLSUCCESS;
    }

    Common::BoolResult
    VulkanMaterial::AddUniform2DToOverride( const std::shared_ptr<Uniforms::UniformImage2D>& uniform2D )
    {
        m_OverriddenUniforms.Images2D.push_back(
             sp_cast<Uniforms::API::Vulkan::VulkanUniformImage2D>( uniform2D ) );
        return BOOLSUCCESS;
    }

    Common::BoolResult VulkanMaterial::ApplyMaterial()
    {
        static constexpr uint32_t SET = 0u;

        uint32_t    frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& vulkanShader = sp_cast<VulkanShader>( m_Shader );

        const auto size = m_OverriddenUniforms.Buffers.size() + m_OverriddenUniforms.ImageCubes.size() +
                          m_OverriddenUniforms.Images2D.size();

        std::vector<VkWriteDescriptorSet> writeDescriptorSets;
        writeDescriptorSets.reserve( size );

        // Uniform
        {
            for ( const auto& uniform : m_OverriddenUniforms.Buffers )
            {
                auto wds =
                     DescriptorSetBuilder::GetUniformWDS( vulkanShader, frameIndex, SET, uniform->GetBinding(), 1U,
                                                          &uniform->GetDescriptorBufferInfo() );
                writeDescriptorSets.push_back( std::move( wds ) );
            }
        }

        // Image2D
        {
            for ( const auto& uniform2D : m_OverriddenUniforms.Images2D )
            {
                auto wds =
                     DescriptorSetBuilder::GetSamplerWDS( vulkanShader, frameIndex, SET, uniform2D->GetBinding(),
                                                          1U, &uniform2D->GetDescriptorImageInfo() );
                writeDescriptorSets.push_back( std::move( wds ) );
            }
        }

        // ImageCube
        {
            for ( const auto& uniformCube : m_OverriddenUniforms.ImageCubes )
            {
                auto wds =
                     DescriptorSetBuilder::GetSamplerWDS( vulkanShader, frameIndex, SET, uniformCube->GetBinding(),
                                                          1U, &uniformCube->GetDescriptorImageInfo() );
                writeDescriptorSets.push_back( std::move( wds ) );
            }
        }

        static_cast<API::Vulkan::VulkanRendererAPI*>( Renderer::GetInstance().GetRendererAPI() )
             ->GetDescriptorManager()
             ->UpdateDescriptorSet( vulkanShader, frameIndex, 0, writeDescriptorSets );

        return BOOLSUCCESS;
    }

    Common::BoolResult VulkanMaterial::PushConstant( const void* buffer, const uint32_t bufferSize )
    {
#ifdef DESERT_CONFIG_DEBUG
        if ( bufferSize > kMaxPushConstantsSize )
        {
            return Common::MakeError( "bufferSize > 128 bytes" );
        }
#endif // DESERT_CONFIG_DEBUG

        m_PushConstantBuffer.Write( buffer, bufferSize );

        return BOOLSUCCESS;
    }

    void VulkanMaterial::Clear()
    {
        m_OverriddenUniforms.Buffers.clear();
        m_OverriddenUniforms.ImageCubes.clear();
        m_OverriddenUniforms.Images2D.clear();
    }

} // namespace Desert::Graphic::API::Vulkan