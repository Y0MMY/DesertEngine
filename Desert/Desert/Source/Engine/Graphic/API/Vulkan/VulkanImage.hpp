#pragma once

#include <Engine/Graphic/Image.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Graphic::API::Vulkan
{
    struct VulkanImageInfo
    {
        VkImage               Image = nullptr;
        VkDescriptorImageInfo ImageInfo{};
        VkFormat              Format;
        VmaAllocation         MemoryAlloc = nullptr;
    };

    VkFormat GetImageVulkanFormat( const Core::Formats::ImageFormat& imageFormat );

    class VulkanImageBase
    {
    public:
        virtual ~VulkanImageBase() = default;

        virtual const VulkanImageInfo& GetVulkanImageInfo() const                  = 0;
        virtual void              TransitionImageLayout( VkCommandBuffer cmdBuffer, VkImageLayout newImageLayout,
                                                         const uint32_t mip = 1U ) = 0;
        virtual const VkImageView GetMipImageView( uint32_t level ) const          = 0;

        virtual uint32_t GetLayerCount() const = 0;
    };

    class VulkanImage2D final : public Image2D, public VulkanImageBase
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

        virtual Core::Formats::ImagePixelData GetImagePixels() override;

        // VulkanImageBase interface
        virtual const VulkanImageInfo& GetVulkanImageInfo() const override
        {
            return m_VulkanImageInfo;
        }

        Common::BoolResultStr RT_Invalidate();
        Common::BoolResultStr Invalidate() override;
        Common::BoolResultStr Release() override;

        void TransitionImageLayout( VkCommandBuffer cmdBuffer, VkImageLayout newImageLayout,
                                    const uint32_t mip = 1U ) override;

        const VkImageView GetMipImageView( uint32_t level ) const override
        {
            return m_MipImageViews[level];
        }

        virtual uint32_t GetLayerCount() const
        {
            return 1U;
        }

    private:
        void               CopyBufferToImage2D( VkCommandBuffer commandBuffer, VkBuffer sourceBuffer );
        Common::BoolResultStr CreateTextureImage( VkDevice device, const VkImageCreateInfo& imageInfo );
        Common::BoolResultStr CreateAttachmentImage( VkDevice device, VkImageCreateInfo& imageInfo, VkFormat format );

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
        ~VulkanImageCube() override;
        explicit VulkanImageCube( const VulkanImageCube& other );

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

        virtual void Use( uint32_t slot = 0 ) const override;

        virtual Core::Formats::ImageCubeSpecification& GetImageSpecification() override
        {
            return m_ImageSpecification;
        }

        virtual Core::Formats::ImagePixelData GetImagePixels() override;

        // VulkanImageBase interface
        virtual const VulkanImageInfo& GetVulkanImageInfo() const override
        {
            return m_VulkanImageInfo;
        }

        Common::BoolResultStr RT_Invalidate();
        Common::BoolResultStr Invalidate() override;
        Common::BoolResultStr Release() override;

        void TransitionImageLayout( VkCommandBuffer cmdBuffer, VkImageLayout newImageLayout,
                                    const uint32_t mip = 1U ) override;

        const VkImageView GetMipImageView( uint32_t level ) const override
        {
            return m_MipImageViews[level];
        }

        virtual uint32_t GetLayerCount() const
        {
            return 6U;
        }

    private:
        Common::BoolResultStr CreateCubemapImage( VkDevice device, const VkImageCreateInfo& imageInfo,
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