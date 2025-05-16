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
        VkImageLayout Layout      = (VkImageLayout)0;
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

        virtual Core::Formats::ImageSpecification& GetImageSpecification() override
        {
            return m_ImageSpecification;
        }

        virtual Core::Formats::ImagePixelData GetImagePixels() const override;

        const auto GetMipImageView( const uint32_t level ) const
        {
            return m_MipImageViews[level];
        }

    private:
        Common::BoolResult GenerateMipmaps( bool readonly ) const;
        void               GenerateMipmaps2D( VkCommandBuffer cmd ) const;
        void               GenerateMipmapsCubemap( VkCommandBuffer cmd ) const;

        VkImageCreateInfo  GetImageCreateInfo( VkFormat imageFormat );
        VkImageCreateInfo  CreateImageInfo( VkFormat format );
        Common::BoolResult CreateAttachmentImage( VkDevice device, VulkanAllocator& allocator,
                                                  VkImageCreateInfo& imageInfo, VkFormat format );
        Common::BoolResult CreateTextureImage( VkDevice device, VulkanAllocator& allocator,
                                               const VkImageCreateInfo& imageInfo, VkFormat format );

        Common::BoolResult CreateStorageImage( VkDevice device, VulkanAllocator& allocator,
                                               const VkImageCreateInfo& imageInfo );

        Common::BoolResult CreateCubemapImage( VkDevice device, VulkanAllocator& allocator,
                                               const VkImageCreateInfo& imageInfo, VkFormat format );

        const VkImageLayout GetVkImageLayout( bool isStorage ) const;

    private:
        uint32_t                          m_MipLevels = 1u;
        std::vector<VkImageView>          m_MipImageViews;
        Core::Formats::ImageSpecification m_ImageSpecification;

        bool m_Loaded = false;

        VulkanImageInfo m_VulkanImageInfo;
    };
} // namespace Desert::Graphic::API::Vulkan