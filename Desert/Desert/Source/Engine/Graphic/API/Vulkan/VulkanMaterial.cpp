#include <Engine/Graphic/API/Vulkan/VulkanMaterial.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
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

        const auto& shaderDS = sp_cast<VulkanShader>( m_Shader )->GetShaderDescriptorSets();
        for ( const auto& descriptor : shaderDS )
        {
            for ( const auto& [_, imageInfo] : descriptor.second.Image2DSamplers )
            {
                m_AvalivaleImages2D.push_back( { imageInfo.Name, imageInfo.BindingPoint } );
            }

            for ( const auto& [_, imageInfo] : descriptor.second.ImageCubeSamplers )
            {
                m_AvalivaleImagesCube.push_back( { imageInfo.Name, imageInfo.BindingPoint } );
            }
        }
        return BOOLSUCCESS;
    }

    // TODO: name -> Uniform buffer info

    Common::BoolResult VulkanMaterial::AddUniformToOverride( const std::shared_ptr<UniformBuffer>& uniformBuffer )
    {
        m_OverriddenUniforms.push_back( sp_cast<VulkanUniformBuffer>( uniformBuffer ) );
        return BOOLSUCCESS;
    }

    /* Common::BoolResult VulkanMaterial::SetVec3( const std::string& name, const glm::vec3& data )
     {
         return SetData( name, &data, sizeof( glm::vec3 ) );
     }

     Common::BoolResult VulkanMaterial::SetMat4( const std::string& name, const glm::mat4& data )
     {
         return SetData( name, &data, sizeof( glm::vec3 ) );
     }*/

    Common::BoolResult VulkanMaterial::ApplyMaterial()
    {
        static constexpr uint32_t SET = 0u;

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        std::vector<VkWriteDescriptorSet> writeDescriptorSets;
        writeDescriptorSets.reserve(
             ( m_OverriddenUniforms.size() + m_OverriddenImages2D.size() + m_OverriddenImagesCube.size() ) );

        // Uniform
        {
            for ( const auto& uniform : m_OverriddenUniforms )
            {
                auto wds = DescriptorSetBuilder::GetUniformWDS( sp_cast<VulkanShader>( m_Shader ), frameIndex, SET,
                                                                uniform->GetBinding(), 1U,
                                                                &uniform->GetDescriptorBufferInfo() );
                writeDescriptorSets.push_back( std::move( wds ) );
            }
        }

        // Image2D
        {
            for ( const auto& imageInfo : m_OverriddenImages2D )
            {
                auto info = sp_cast<VulkanImage2D>( imageInfo.Image2D )->GetVulkanImageInfo();

                VkDescriptorImageInfo imageDescriptorInfo = {};
                imageDescriptorInfo.sampler               = info.Sampler;
                imageDescriptorInfo.imageView             = info.ImageView;
                imageDescriptorInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                auto wds = DescriptorSetBuilder::GetSamplerWDS( sp_cast<VulkanShader>( m_Shader ), frameIndex, SET,
                                                                imageInfo.Binding, 1U, &imageDescriptorInfo );
                writeDescriptorSets.push_back( std::move( wds ) );
            }
        }

        // ImageCube
        {
            for ( const auto& imageInfo : m_OverriddenImagesCube )
            {
                auto info = sp_cast<VulkanImageCube>( imageInfo.ImageCube )->GetVulkanImageInfo();

                VkDescriptorImageInfo imageDescriptorInfo = {};
                imageDescriptorInfo.sampler               = info.Sampler;
                imageDescriptorInfo.imageView             = info.ImageView;
                imageDescriptorInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                auto wds = DescriptorSetBuilder::GetSamplerWDS( sp_cast<VulkanShader>( m_Shader ), frameIndex, SET,
                                                                imageInfo.Binding, 1U, &imageDescriptorInfo );
                writeDescriptorSets.push_back( std::move( wds ) );
            }
        }

        const VkDevice device = API::Vulkan::VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        vkUpdateDescriptorSets( device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr );

        return BOOLSUCCESS;
    }

    Common::BoolResult VulkanMaterial::SetImage2D( const std::string&              name,
                                                   const std::shared_ptr<Image2D>& image2D )
    {
        auto it = std::ranges::find( m_AvalivaleImages2D, name, []( const auto& pair ) { return pair.first; } );
#ifdef DESERT_CONFIG_DEBUG
        if ( it == m_AvalivaleImages2D.end() )
        {
            return Common::MakeFormattedError( "Image '{}' not found in material", name );
        }
#endif // DESERT_CONFIG_DEBUG

        m_OverriddenImages2D.push_back( { image2D, it->second } );

        return BOOLSUCCESS;
    }

    Common::BoolResult VulkanMaterial::SetImageCube( const std::string&                name,
                                                     const std::shared_ptr<ImageCube>& imageCube )
    {
        auto it = std::ranges::find( m_AvalivaleImagesCube, name, []( const auto& pair ) { return pair.first; } );
#ifdef DESERT_CONFIG_DEBUG
        if ( it == m_AvalivaleImagesCube.end() )
        {
            return Common::MakeFormattedError( "Image '{}' not found in material", name );
        }
#endif // DESERT_CONFIG_DEBUG

        m_OverriddenImagesCube.push_back( { imageCube, it->second } );

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
        m_OverriddenUniforms.clear();
        m_AvalivaleImages2D.clear();
        m_AvalivaleImagesCube.clear();
    }

} // namespace Desert::Graphic::API::Vulkan