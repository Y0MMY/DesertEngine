#include <Engine/ShaderResources/API/Vulkan/VulkanUniformImage2D.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

namespace Desert::ShaderResources::API::Vulkan
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

    void VulkanUniformImage2D::SetImage2D( const Graphic::Image2D* image2D )
    {
        m_Image2D = image2D;

        if ( image2D )
        {
            const auto& res              = ( (Graphic::API::Vulkan::VulkanImage2D*)image2D )->GetResource();
            m_DescriptorInfo.imageView   = res.ImageView;
            m_DescriptorInfo.sampler     = res.Sampler;
            m_DescriptorInfo.imageLayout = res.Layout;
        }
        else
        {
            m_DescriptorInfo = {};
        }
    }

} // namespace Desert::ShaderResources::API::Vulkan
