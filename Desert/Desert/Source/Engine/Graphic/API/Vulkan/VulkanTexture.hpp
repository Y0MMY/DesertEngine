#pragma once

#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanTexture2D final : public Texture2D
    {
    public:
        VulkanTexture2D( const TextureSpecification& specification, const std::filesystem::path& path );

        virtual const std::shared_ptr<Image2D>& GetImage2D() const override
        {
            return m_Image2D;
        }

        virtual Common::BoolResult Invalidate() override;

    private:
        const std::filesystem::path m_TexturePath;
        const TextureSpecification& m_Specification;

        std::shared_ptr<Image2D> m_Image2D;
    };

    class VulkanTextureCube final : public TextureCube
    {
    public:
        VulkanTextureCube( const TextureSpecification& specification, const std::filesystem::path& path );

        virtual const std::shared_ptr<Image2D>& GetImage2D() const override
        {
            return m_Image2D;
        }

        virtual Common::BoolResult Invalidate() override;

    private:
        const std::filesystem::path m_TexturePath;
        const TextureSpecification& m_Specification;

        std::shared_ptr<Image2D> m_Image2D;
    };
} // namespace Desert::Graphic::API::Vulkan