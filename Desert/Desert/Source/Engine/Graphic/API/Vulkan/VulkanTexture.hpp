#pragma once

#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanTexture2D final : public Texture2D
    {
    public:
        VulkanTexture2D( const std::filesystem::path& path );

        virtual const std::shared_ptr<Image2D>& GetImage2D() const override
        {
            return m_Image2D;
        }

    private:
        std::filesystem::path m_TexturePath;

        std::shared_ptr<Image2D> m_Image2D;
    };
} // namespace Desert::Graphic::API::Vulkan