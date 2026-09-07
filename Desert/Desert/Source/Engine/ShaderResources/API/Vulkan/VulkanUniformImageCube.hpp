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

        virtual const Common::UUID GetImageHash() const override
        {
            return m_ImageCube->GetHash();
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
        VkDescriptorImageInfo     m_DescriptorInfo{};
        const std::string         m_DebugName;
        uint32_t                  m_Binding = 0;
        // Initialised, because it was not: the constructor left this indeterminate and
        // GetImageHash() dereferences it, so a hash asked for before the first SetImage read
        // through whatever the stack held. nullptr does not make that call correct -- it makes
        // it a crash at the line that is wrong instead of a UUID out of uninitialised memory.
        const Graphic::ImageCube* m_ImageCube = nullptr;
    };
} // namespace Desert::ShaderResources::API::Vulkan