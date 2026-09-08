#include <Engine/ShaderResources/API/Vulkan/VulkanUniformImageCube.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

namespace Desert::ShaderResources::API::Vulkan
{

    VulkanUniformImageCube::VulkanUniformImageCube( const std::string_view debugName, uint32_t binding )
         : m_DebugName( debugName ), m_Binding( binding )
    {
    }

    VulkanUniformImageCube::~VulkanUniformImageCube()
    {
    }

    void VulkanUniformImageCube::SetImageCube( const Graphic::ImageCube* imageCube )
    {
        m_ImageCube = imageCube;
        if ( !m_ImageCube )
        {
            m_DescriptorInfo = {};
            return;
        }

        const auto& res              = ( (Graphic::API::Vulkan::VulkanImageCube*)imageCube )->GetResource();
        m_DescriptorInfo.imageView   = res.ImageView;
        m_DescriptorInfo.sampler     = res.Sampler;
        m_DescriptorInfo.imageLayout = res.Layout;
    }

} // namespace Desert::ShaderResources::API::Vulkan
