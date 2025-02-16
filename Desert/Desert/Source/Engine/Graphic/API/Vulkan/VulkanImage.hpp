#pragma once

#include <Engine/Graphic/Image.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Graphic::API::Vulkan
{
    struct VulkanImageInfo
    {
        VkImage       Image       = nullptr;
        VkImageView   ImageView   = nullptr;
        VkSampler     Sampler     = nullptr;
        VmaAllocation MemoryAlloc = nullptr;
    };

    VkFormat GetImageVulkanFormat( const Core::Formats::ImageFormat& imageFormat );

    class VulkanImage2D final : public Image2D
    {
    public:
        VulkanImage2D( const Core::Formats::ImageSpecification& specfication );

        virtual uint32_t GetWidth() const override
        {
            return m_ImageSpecification.Width;
        }
        virtual uint32_t GetHeight() const override
        {
            return m_ImageSpecification.Height;
        }
        virtual Core::Formats::ImageFormat GetImageFormat() const override
        {
            return m_ImageSpecification.Format;
        }
        virtual uint32_t GetMipmapLevels() const override;
        virtual bool     IsLoaded() const override
        {
            return m_Loaded;
        }

        const VulkanImageInfo GetVulkanImageInfo() const
        {
            return m_VulkanImageInfo;
        }

        Common::BoolResult RT_Invalidate();

        virtual void Use( uint32_t slot = 0 ) const override;

    private:
        VkImageCreateInfo GetImageCreateInfo( VkFormat imageFormat );

    private:
        uint32_t m_MipLevels = 0u;
        Core::Formats::ImageSpecification m_ImageSpecification;

        bool m_Loaded = false;

        VulkanImageInfo m_VulkanImageInfo;
    };
} // namespace Desert::Graphic::API::Vulkan