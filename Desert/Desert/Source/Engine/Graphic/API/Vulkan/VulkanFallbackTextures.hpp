#pragma once

#include <Engine/Graphic/FallbackTextures.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanFallbackTextures final : public FallbackTextures
    {
    public:
        VulkanFallbackTextures();

        [[NODISCARD]] virtual Common::BoolResultStr Invalidate() override
        {
            return BOOLSUCCESS;
        }
        [[NODISCARD]] virtual Common::BoolResultStr Release() override;

        virtual const std::shared_ptr<Image2D>&
        GetFallbackTexture2D( Core::Formats::ImageFormat format ) const override;
        virtual const std::shared_ptr<ImageCube>&
        GetFallbackTextureCube( Core::Formats::ImageFormat format ) const override;

    private:
        void CreateFallbackTexture2D( Core::Formats::ImageFormat format );
        void CreateFallbackTextureCube( Core::Formats::ImageFormat format );

    private:
        std::unordered_map<Core::Formats::ImageFormat, std::shared_ptr<Image2D>>   m_FallbackTextures2D;
        std::unordered_map<Core::Formats::ImageFormat, std::shared_ptr<ImageCube>> m_FallbackTexturesCube;
    };
} // namespace Desert::Graphic::API::Vulkan