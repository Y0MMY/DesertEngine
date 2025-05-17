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

    class VulkanImageBase : public Image
    {
    public:
        virtual const VulkanImageInfo& GetVulkanImageInfo() const = 0;
        virtual uint32_t               GetMipmapLevels() const    = 0;
        virtual Common::BoolResult     RT_Invalidate()            = 0;
    };

    class VulkanImage2D final : public Image2D, public VulkanImageBase
    {
    public:
        VulkanImage2D( const Core::Formats::Image2DSpecification& specification );

        // Image2D interface
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

        virtual uint32_t GetMipmapLevels() const override
        {
            return m_MipLevels;
        }

        virtual bool IsLoaded() const override
        {
            return m_Loaded;
        }

        virtual void Use( uint32_t slot = 0 ) const override;

        virtual Core::Formats::Image2DSpecification& GetImageSpecification() override
        {
            return m_ImageSpecification;
        }

        virtual Core::Formats::ImagePixelData GetImagePixels() const override;

        // VulkanImageBase interface
        virtual const VulkanImageInfo& GetVulkanImageInfo() const override
        {
            return m_VulkanImageInfo;
        }

        Common::BoolResult RT_Invalidate() override;

        const auto GetMipImageView( uint32_t level ) const
        {
            return m_MipImageViews[level];
        }

    private:
        Common::BoolResult CreateTextureImage( VkDevice device, VulkanAllocator& allocator,
                                               const VkImageCreateInfo& imageInfo, VkFormat format );
        Common::BoolResult CreateAttachmentImage( VkDevice device, VulkanAllocator& allocator,
                                                  VkImageCreateInfo& imageInfo, VkFormat format );

        VkImageCreateInfo CreateImageInfo( VkFormat format );

    private:
        uint32_t                            m_MipLevels = 1u;
        std::vector<VkImageView>            m_MipImageViews;
        Core::Formats::Image2DSpecification m_ImageSpecification;
        bool                                m_Loaded = false;
        VulkanImageInfo                     m_VulkanImageInfo;
    };

    class VulkanImageCube final : public ImageCube, public VulkanImageBase
    {
    public:
        VulkanImageCube( const Core::Formats::ImageCubeSpecification& specification );

        // ImageCube interface
        virtual uint32_t GetWidth() const override
        {
            return m_FaceSize;
        }
        virtual uint32_t GetHeight() const override
        {
            return m_FaceSize;
        }
        virtual Core::Formats::ImageFormat GetImageFormat() const override
        {
            return m_ImageSpecification.Format;
        }
        virtual uint32_t GetMipmapLevels() const override
        {
            return m_MipLevels;
        }
        virtual bool IsLoaded() const override
        {
            return m_Loaded;
        }
        virtual void                                   Use( uint32_t slot = 0 ) const override;
        virtual Core::Formats::ImageCubeSpecification& GetImageSpecification() override
        {
            return m_ImageSpecification;
        }
        virtual Core::Formats::ImagePixelData GetImagePixels() const override;

        // VulkanImageBase interface
        virtual const VulkanImageInfo& GetVulkanImageInfo() const override
        {
            return m_VulkanImageInfo;
        }

        Common::BoolResult RT_Invalidate() override;

        const auto GetMipImageView( uint32_t level ) const
        {
            return m_MipImageViews[level];
        }

    private:
        Common::BoolResult CreateCubemapImage( VkDevice device, VulkanAllocator& allocator,
                                               const VkImageCreateInfo& imageInfo, VkFormat format );
        VkImageCreateInfo  CreateImageInfo( VkFormat format );

    private:
        uint32_t                              m_FaceSize  = 0u;
        uint32_t                              m_MipLevels = 1u;
        std::vector<VkImageView>              m_MipImageViews;
        Core::Formats::ImageCubeSpecification m_ImageSpecification;
        bool                                  m_Loaded = false;
        VulkanImageInfo                       m_VulkanImageInfo;
    };
} // namespace Desert::Graphic::API::Vulkan