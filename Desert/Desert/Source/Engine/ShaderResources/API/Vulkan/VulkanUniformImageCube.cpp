#include <Engine/ShaderResources/API/Vulkan/VulkanUniformImageCube.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>

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

        // NULL IS A VALUE, NOT A REFUSAL, AND THAT IS THE Г14 FIX. This used to blank the descriptor info
        // and return, which paired with a caller (TextureCubeProperty::Apply) that only wrote the
        // descriptor when it had an image — so "this scene has no environment" reached here and then
        // reached nothing. The descriptor kept the LAST cube any scene had put in it, and the next scene
        // was shaded by the previous scene's sky: CornellDemo -> Clouds_Protocol -> CornellDemo came back
        // 100 % different, mean 0.477 -> 0.794, green-wall saturation 0.626 -> 0.313.
        //
        // The image a binding points at when nothing is bound already has an owner — the same fallback
        // VulkanMaterialBackend seeds every declared cube binding with at descriptor-set creation. Pointing
        // back at it is what makes "none" expressible, and makes the FIRST time a scene is shown and the
        // SECOND time bit-identical by construction rather than by luck.
        // RGBA8F is the format the set-creation seed uses (VulkanMaterialBackend's SampledCube arm), so
        // this points at the very same VkImageView the descriptor was born holding.
        const Graphic::ImageCube* bound = imageCube
                                               ? imageCube
                                               : Graphic::FallbackTextures::Get()
                                                      .GetFallbackTextureCube( Core::Formats::ImageFormat::RGBA8F )
                                                      .get();

        const auto& res              = ( (Graphic::API::Vulkan::VulkanImageCube*)bound )->GetResource();
        m_DescriptorInfo.imageView   = res.ImageView;
        m_DescriptorInfo.sampler     = res.Sampler;
        m_DescriptorInfo.imageLayout = res.Layout;
    }

} // namespace Desert::ShaderResources::API::Vulkan
