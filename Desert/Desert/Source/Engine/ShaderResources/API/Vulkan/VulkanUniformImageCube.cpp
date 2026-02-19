#include <Engine/ShaderResources/API/Vulkan/VulkanUniformImageCube.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

namespace Desert::ShaderResources::API::Vulkan
{

    VulkanUniformImageCube::VulkanUniformImageCube( const std::string_view debugName, uint32_t binding )
         : m_DebugName( debugName ), m_Binding( binding )
    {
        RT_Invalidate();
    }

    VulkanUniformImageCube::~VulkanUniformImageCube()
    {
        Release();
    }

    void VulkanUniformImageCube::Release()
    {
    }

    void VulkanUniformImageCube::RT_Invalidate()
    {
    }

    void VulkanUniformImageCube::SetImageCube( const Graphic::ImageCube* imageCube )
    {
        m_ImageCube = imageCube;
        if ( !m_ImageCube )
        {
            return;
        }

        const auto& vulkanImageInfo  = ( (Graphic::API::Vulkan::VulkanImageCube*)imageCube )->GetVulkanImageInfo();
        m_DescriptorInfo.imageView   = vulkanImageInfo.ImageInfo.imageView;
        m_DescriptorInfo.sampler     = vulkanImageInfo.ImageInfo.sampler;
        m_DescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // vulkanImageInfo.Layout;
    }

} // namespace Desert::ShaderResources::API::Vulkan