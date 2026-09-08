#pragma once

#include <Engine/ShaderResources/UniformImageCube.hpp>
#include <Common/Core/Memory/Buffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::ShaderResources::API::Vulkan
{
    class VulkanUniformImageCube : public UniformImageCube
    {
    public:
        VulkanUniformImageCube( const std::string_view debugName, uint32_t binding );
        virtual ~VulkanUniformImageCube();

        virtual uint32_t GetBinding() const override
        {
            return m_Binding;
        }

        const VkDescriptorImageInfo& GetDescriptorImageInfo() const
        {
            return m_DescriptorInfo;
        }

        void SetImageCube( const Graphic::ImageCube* imageCube ) override;

    private:
        // `RT_Invalidate()` and `Release()` stood here with EMPTY BODIES, called from the constructor and
        // the destructor respectively. This class owns no device resource at all — it holds a
        // VkDescriptorImageInfo pointing at an image somebody else created and destroys — so there was
        // never anything for either to do. An empty body "so it links" is contract §1.2, and a `void`
        // resource-creation entry point is also a hole in the GpuWriteCensus rule that every such entry
        // point must be able to refuse. Deleted rather than given a return type: a function with nothing
        // to fail at does not need a channel, it needs to not exist.

    private:
        VkDescriptorImageInfo m_DescriptorInfo{};
        const std::string     m_DebugName;
        uint32_t              m_Binding = 0;
        // `m_ImageCube` stood here and its comment named its ONLY reader, GetImageHash(). Г12 removed
        // that reader, leaving the member written by SetImageCube and read by nobody, so it went too —
        // same as VulkanUniformImage2D. What this class keeps of a cube is m_DescriptorInfo above.
    };
} // namespace Desert::ShaderResources::API::Vulkan