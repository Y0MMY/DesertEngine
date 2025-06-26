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
        VulkanImage2D( const Core::Formats::Image2DSpecification& specification );
        ~VulkanImage2D() override;

        // Image2D interface
        virtual uint32_t GetWidth() const override
        {
            return m_ImageSpecification.Width;
        }

        virtual uint32_t GetHeight() const override
        {
            return m_ImageSpecification.Height;
        }

        virtual const std::string GetHash() const override
        {
            return "TODO";
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
        virtual const VulkanImageInfo& GetVulkanImageInfo() const 
        {
            return m_VulkanImageInfo;
        }

        Common::BoolResult RT_Invalidate();
        Common::BoolResult Invalidate() override;
        Common::BoolResult Release() override;

        const auto GetMipImageView( uint32_t level ) const
        {
            return m_MipImageViews[level];
        }

    private:
        Common::BoolResult CreateTextureImage( VkDevice device, const VkImageCreateInfo& imageInfo,
                                               VkFormat format );
        Common::BoolResult CreateAttachmentImage( VkDevice device, VkImageCreateInfo& imageInfo, VkFormat format );

        VkImageCreateInfo CreateImageInfo( VkFormat format );

    private:
        uint32_t                            m_MipLevels = 1u;
        std::vector<VkImageView>            m_MipImageViews;
        Core::Formats::Image2DSpecification m_ImageSpecification;
        bool                                m_Loaded = false;
        VulkanImageInfo                     m_VulkanImageInfo;
    };

    class VulkanImageCube final : public ImageCube
    {
    public:
        VulkanImageCube( const Core::Formats::ImageCubeSpecification& specification );
        ~VulkanImageCube() override;

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

        virtual const std::string GetHash() const override
        {
            return "TODO";
        }

        virtual bool IsLoaded() const override
        {
            return m_Loaded;
        }

        virtual void Use( uint32_t slot = 0 ) const override;

        virtual Core::Formats::ImageCubeSpecification& GetImageSpecification() override
        {
            return m_ImageSpecification;
        }

        virtual Core::Formats::ImagePixelData GetImagePixels() const override;

        // VulkanImageBase interface
        virtual const VulkanImageInfo& GetVulkanImageInfo() const 
        {
            return m_VulkanImageInfo;
        }

        Common::BoolResult RT_Invalidate();
        Common::BoolResult Invalidate() override;
        Common::BoolResult Release() override;

        const auto GetMipImageView( uint32_t level ) const
        {
            return m_MipImageViews[level];
        }

    private:
        Common::BoolResult CreateCubemapImage( VkDevice device, const VkImageCreateInfo& imageInfo,
                                               VkFormat format );
        VkImageCreateInfo  CreateImageInfo( VkFormat format );

        void CopyStagingToGpuImage( VkCommandBuffer commandBuffer, VkBuffer stagingBuffer, VkFormat format,
                                    uint32_t faceSize );

        void CopyImageDataToCubemapFaces( const uint8_t* srcData, uint8_t* dstData, uint32_t srcRowPitch,
                                          uint32_t faceSize, uint32_t bytesPerPixel );

    private:
        uint32_t                              m_FaceSize  = 0u;
        uint32_t                              m_MipLevels = 1u;
        std::vector<VkImageView>              m_MipImageViews;
        Core::Formats::ImageCubeSpecification m_ImageSpecification;
        bool                                  m_Loaded = false;
        VulkanImageInfo                       m_VulkanImageInfo;
    };
} // namespace Desert::Graphic::API::Vulkan