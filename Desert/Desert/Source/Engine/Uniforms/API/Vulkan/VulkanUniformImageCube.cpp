#include <Engine/Uniforms/API/Vulkan/VulkanUniformImageCube.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

namespace Desert::Uniforms::API::Vulkan
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

    void VulkanUniformImageCube::SetImageCube( const std::shared_ptr<Graphic::ImageCube>& imageCube ) 
    {
        m_ImageCube = imageCube;

        const auto& vulkanImageInfo =
             sp_cast<Graphic::API::Vulkan::VulkanImageCube>( imageCube )->GetVulkanImageInfo();
        m_DescriptorInfo.imageView   = vulkanImageInfo.ImageView;
        m_DescriptorInfo.sampler     = vulkanImageInfo.Sampler;
        m_DescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; //vulkanImageInfo.Layout;
    }

} // namespace Desert::Uniforms::API::Vulkan