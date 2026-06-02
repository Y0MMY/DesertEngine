#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace Desert::Graphic::API::Vulkan
{
    /**
     * @brief Internal POD structure for Vulkan image handles and state.
     */
    struct VulkanImageResource
    {
        VkImage               Image        = VK_NULL_HANDLE;
        VkImageView           ImageView    = VK_NULL_HANDLE;
        VkSampler             Sampler      = VK_NULL_HANDLE;
        VmaAllocation         Allocation   = nullptr;
        VkFormat              Format       = VK_FORMAT_UNDEFINED;
        VkImageLayout         Layout       = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t              MipLevels    = 1;
        uint32_t              LayerCount   = 1;

        VkDescriptorImageInfo GetDescriptorInfo() const
        {
            return { Sampler, ImageView, Layout };
        }
    };

    VkFormat GetImageVulkanFormat( const Core::Formats::ImageFormat& format );

    /**
     * @brief Base interface for Vulkan-specific image operations.
     */
    class IVulkanImage
    {
    public:
        virtual ~IVulkanImage() = default;

        [[nodiscard]] virtual const VulkanImageResource& GetResource() const = 0;
        
        virtual void TransitionLayout( VkCommandBuffer cmdBuffer, VkImageLayout newLayout, uint32_t mip = 0 ) = 0;
        
        [[nodiscard]] virtual VkImageView GetMipView( uint32_t level ) const = 0;
    };

    /**
     * @brief implementation of a 2D Vulkan Image.
     */
    class VulkanImage2D final : public Image2D, public IVulkanImage
    {
    public:
        explicit VulkanImage2D( const Core::Formats::Image2DSpecification& spec );
        ~VulkanImage2D() override;

        // --- Image2D Interface ---
        [[nodiscard]] uint32_t GetWidth() const override { return m_Specification.Width; }
        [[nodiscard]] uint32_t GetHeight() const override { return m_Specification.Height; }
        [[nodiscard]] Core::Formats::ImageFormat GetImageFormat() const override { return m_Specification.Format; }
        [[nodiscard]] uint32_t GetMipmapLevels() const override { return m_Resource.MipLevels; }
        [[nodiscard]] bool IsLoaded() const override { return m_IsLoaded; }
        [[nodiscard]] Core::Formats::Image2DSpecification& GetImageSpecification() override { return m_Specification; }
        [[nodiscard]] Core::Formats::ImagePixelData GetImagePixels() override;
        
        void Use( uint32_t slot = 0 ) const override;
        Common::BoolResultStr Invalidate() override;
        Common::BoolResultStr Release() override;

        // --- IVulkanImage Interface ---
        [[nodiscard]] const VulkanImageResource& GetResource() const override { return m_Resource; }
        void TransitionLayout( VkCommandBuffer cmdBuffer, VkImageLayout newLayout, uint32_t mip = 0 ) override;
        [[nodiscard]] VkImageView GetMipView( uint32_t level ) const override;

        // --- Legacy compatibility (TODO: remove) ---
        [[nodiscard]] const VulkanImageResource& GetVulkanImageInfo() const { return m_Resource; }

        // --- Vulkan Specific ---
        Common::BoolResultStr RT_Invalidate();

    private:
        Common::BoolResultStr CreateResource();
        void UploadData( VkCommandBuffer cmdBuffer, VkBuffer stagingBuffer );

    private:
        Core::Formats::Image2DSpecification m_Specification;
        VulkanImageResource                 m_Resource;
        std::vector<VkImageView>            m_MipViews;
        bool                                m_IsLoaded = false;
    };

    /**
     * @brief implementation of a Cubemap Vulkan Image.
     */
    class VulkanImageCube final : public ImageCube, public IVulkanImage
    {
    public:
        explicit VulkanImageCube( const Core::Formats::ImageCubeSpecification& spec );
        ~VulkanImageCube() override;

        // --- ImageCube Interface ---
        [[nodiscard]] uint32_t GetWidth() const override { return m_Specification.Width / 4; } // Assumes 4x3 layout
        [[nodiscard]] uint32_t GetHeight() const override { return m_Specification.Height / 3; }
        [[nodiscard]] Core::Formats::ImageFormat GetImageFormat() const override { return m_Specification.Format; }
        [[nodiscard]] uint32_t GetMipmapLevels() const override { return m_Resource.MipLevels; }
        [[nodiscard]] bool IsLoaded() const override { return m_IsLoaded; }
        [[nodiscard]] Core::Formats::ImageCubeSpecification& GetImageSpecification() override { return m_Specification; }
        [[nodiscard]] Core::Formats::ImagePixelData GetImagePixels() override;

        void Use( uint32_t slot = 0 ) const override;
        Common::BoolResultStr Invalidate() override;
        Common::BoolResultStr Release() override;

        // --- IVulkanImage Interface ---
        [[nodiscard]] const VulkanImageResource& GetResource() const override { return m_Resource; }
        void TransitionLayout( VkCommandBuffer cmdBuffer, VkImageLayout newLayout, uint32_t mip = 0 ) override;
        [[nodiscard]] VkImageView GetMipView( uint32_t level ) const override;

        // --- Legacy compatibility (TODO: remove) ---
        [[nodiscard]] const VulkanImageResource& GetVulkanImageInfo() const { return m_Resource; }

        // --- Vulkan Specific ---
        Common::BoolResultStr RT_Invalidate();

    private:
        Common::BoolResultStr CreateResource();
        void UploadData( VkCommandBuffer cmdBuffer, VkBuffer stagingBuffer );

    private:
        Core::Formats::ImageCubeSpecification m_Specification;
        VulkanImageResource                   m_Resource;
        std::vector<VkImageView>              m_MipViews;
        bool                                  m_IsLoaded = false;
    };

} // namespace Desert::Graphic::API::Vulkan
