#include <Engine/Uniforms/API/Vulkan/VulkanUniformImage2D.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

namespace Desert::Uniforms::API::Vulkan
{

    VulkanUniformImage2D::VulkanUniformImage2D( const std::string_view debugName, uint32_t binding )
         : m_DebugName( debugName ), m_Binding( binding )
    {
        RT_Invalidate();
    }

    VulkanUniformImage2D::~VulkanUniformImage2D()
    {
        Release();
    }

    void VulkanUniformImage2D::Release()
    {
    }

    void VulkanUniformImage2D::RT_Invalidate()
    {
    }

    void VulkanUniformImage2D::SetImage2D( const std::shared_ptr<Graphic::Image2D>& image2D )
    {
        m_Image2D = image2D;

        const auto& vulkanImageInfo =
             sp_cast<Graphic::API::Vulkan::VulkanImage2D>( m_Image2D )->GetVulkanImageInfo();
        m_DescriptorInfo.imageView   = vulkanImageInfo.ImageInfo.imageView;
        m_DescriptorInfo.sampler     = vulkanImageInfo.ImageInfo.sampler;
        m_DescriptorInfo.imageLayout = vulkanImageInfo.ImageInfo.imageLayout;
    }

} // namespace Desert::Uniforms::API::Vulkan