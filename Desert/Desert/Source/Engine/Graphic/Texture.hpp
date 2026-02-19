#pragma once

#include <Engine/Graphic/Image.hpp>
#include <filesystem>

#include <Engine/Core/Formats/ImageFormat.hpp>
#include <Engine/Runtime/ImageHandle.hpp>

namespace Desert::Graphic
{
    struct TextureSpecification
    {
        bool GenerateMips = true;
    };

    class Texture
    {
    public:
        virtual ~Texture() = default;

        virtual uint32_t GetWidth() const  = 0;
        virtual uint32_t GetHeight() const = 0;

        virtual const Runtime::ImageHandle& GetImageHandle() const = 0;
    };

    class Texture2D final : public Texture
    {
    public:
        Texture2D( const TextureSpecification& specification, const std::filesystem::path& path );
        virtual ~Texture2D() = default;

        static constexpr Core::Formats::Image2DUsage Type = Core::Formats::Image2DUsage::Image2D;

        virtual uint32_t GetWidth() const override
        {
            return m_Width;
        }
        virtual uint32_t GetHeight() const override
        {
            return m_Height;
        }

        virtual const Runtime::ImageHandle& GetImageHandle() const override
        {
            return m_Handle;
        }

        static Common::ResultStr<std::shared_ptr<Texture2D>> Create( const TextureSpecification&  specification,
                                                                     const std::filesystem::path& path );

    private:
        Common::BoolResultStr Invalidate();

    private:
        std::filesystem::path m_TexturePath;
        TextureSpecification  m_Specification;

        Runtime::ImageHandle m_Handle;
        uint32_t             m_Width = 0, m_Height = 0;
    };

    class TextureCube final : public Texture
    {
    public:
        TextureCube( const TextureSpecification& specification, const std::filesystem::path& path );
        virtual ~TextureCube() = default;

        virtual uint32_t GetWidth() const override
        {
            return m_Width;
        }
        virtual uint32_t GetHeight() const override
        {
            return m_Height;
        }

        virtual const Runtime::ImageHandle& GetImageHandle() const override
        {
            return m_Handle;
        }

        static Common::ResultStr<std::shared_ptr<TextureCube>> Create( const TextureSpecification&  specification,
                                                                       const std::filesystem::path& path );

    private:
        Common::BoolResultStr Invalidate();

    private:
        std::filesystem::path m_TexturePath;
        TextureSpecification  m_Specification;

        Runtime::ImageHandle m_Handle;
        uint32_t             m_Width = 0, m_Height = 0;
    };

} // namespace Desert::Graphic
