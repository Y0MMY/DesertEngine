#include <Engine/Graphic/API/Vulkan/VulkanMaterial.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/Renderer.hpp>

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

    VulkanMaterial::VulkanMaterial( const std::string& debugName, const std::shared_ptr<Shader>& shader )
         : m_Shader( shader ), m_DebugName( debugName )
    {
    }

    Common::BoolResult VulkanMaterial::Invalidate()
    {
        if ( !m_Shader )
        {
            return Common::MakeFormattedError( "{}: shader was not attached", m_DebugName );
        }

        m_Uniforms.clear();

        const auto& shaderDS = sp_cast<VulkanShader>( m_Shader )->GetShaderDescriptorSets();
        for ( const auto& descriptor : shaderDS )
        {
            for ( const auto& uniform : descriptor.second.UniformBuffers )
            {
                const auto& uniformInfo = uniform.second.first;
                auto        buffer = std::make_shared<VulkanUniformBuffer>( uniformInfo.Size, descriptor.first );

                if ( !buffer )
                {
                    return Common::MakeError( "Failed to create uniform buffer" );
                }

                m_Uniforms[uniformInfo.Name] = { buffer, 0, uniformInfo.Size, uniformInfo.BindingPoint };
            }

            for ( const auto& image : descriptor.second.ImageSamplers )
            {
                const auto& imageInfo = image.second.first;

                m_Images2D[imageInfo.Name] = { nullptr, imageInfo.BindingPoint };
            }
        }

        return BOOLSUCCESS;
    }

    Common::BoolResult VulkanMaterial::SetData( const std::string& name, const void* data, const uint32_t size )
    {
        auto it = m_Uniforms.find( name );
        if ( it == m_Uniforms.end() )
        {
            return Common::MakeFormattedError( "Uniform '{}' not found in material", name );
        }

        auto& uniform = it->second;
        if ( size > uniform.Size )
        {
            return Common::MakeFormattedError( "Data size {} exceeds uniform '{}' size {}", size, name,
                                               uniform.Size );
        }

        uniform.Buffer->SetData( data, size, uniform.Offset );

        return BOOLSUCCESS;
    }

    Common::BoolResult VulkanMaterial::SetVec3( const std::string& name, const glm::vec3& data )
    {
        return SetData( name, &data, sizeof( glm::vec3 ) );
    }

    Common::BoolResult VulkanMaterial::SetMat4( const std::string& name, const glm::mat4& data )
    {
        return SetData( name, &data, sizeof( glm::vec3 ) );
    }

    Common::BoolResult VulkanMaterial::ApplyMaterial()
    {
        static constexpr uint32_t SET = 0u;

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        std::vector<VkWriteDescriptorSet> writeDescriptorSets;
        writeDescriptorSets.reserve( ( m_Uniforms.size() + m_Images2D.size() ) );

        // Uniform
        {
            for ( const auto& uniform : m_Uniforms )
            {
                auto wds = sp_cast<VulkanShader>( m_Shader )
                                ->GetWriteDescriptorSet( API::Vulkan::WriteDescriptorType::Uniform,
                                                         uniform.second.Binding, SET, frameIndex );
                const auto bufferInfo = uniform.second.Buffer->GetDescriptorBufferInfo();
                wds.pBufferInfo       = &bufferInfo;
                writeDescriptorSets.push_back( std::move( wds ) );
            }
        }

        // Image2D
        {
            for ( const auto& image : m_Images2D )
            {
                const auto& imageInfo = image.second;
                auto        info      = sp_cast<VulkanImage2D>( imageInfo.Image2D )->GetVulkanImageInfo();

                VkDescriptorImageInfo imageDescriptorInfo = {};
                imageDescriptorInfo.sampler               = info.Sampler;
                imageDescriptorInfo.imageView             = info.ImageView;
                imageDescriptorInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                auto wds = sp_cast<VulkanShader>( m_Shader )
                                ->GetWriteDescriptorSet( API::Vulkan::WriteDescriptorType::Sampler2D,
                                                         imageInfo.Binding, SET, frameIndex );

                wds.pImageInfo = &imageDescriptorInfo;
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
        auto it = m_Images2D.find( name );
        if ( it == m_Images2D.end() )
        {
            return Common::MakeFormattedError( "Image '{}' not found in material", name );
        }

        m_Images2D[name].Image2D = image2D;
    }

} // namespace Desert::Graphic::API::Vulkan