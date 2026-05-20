#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Graphic/Texture.hpp>

#include <Engine/Assets/AssetManager.hpp>

#include <Engine/Core/Formats/ImageFormat.hpp>

namespace Desert::Assets
{
    class TextureAsset : public AssetBase
    {
    public:
        // TODO: Move TextureAsset::Type to models
        enum class Type
        {
            Albedo,
            Normal,
            Metallic,
            Roughness,
            AO,
            Emissive,
            //******//
            Skybox
        };

        explicit TextureAsset( AssetPriority priority, const Common::Filepath& filepath /*, Type type*/ );

        virtual Common::BoolResultStr Load() override;

        virtual Common::BoolResultStr Unload() override;

        virtual bool IsReadyForUse() const
        {
            return m_IsReadyForUse;
        }

        Type GetType() const
        {
            return m_Type;
        }

        const auto& GetSourcePath() const
        {
            return m_SourcePath;
        }

        const auto& GetHandle() const
        {
            return m_Handle;
        }

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Texture2D;
        }

    private:
        Type         m_Type;
        bool         m_IsReadyForUse = false;
        Common::UUID m_Handle;
        std::string  m_SourcePath;
        std::string  m_CookedPath;

        uint32_t m_Width;
        uint32_t m_Height;
        uint32_t m_Channels;

        Desert::Core::Formats::ImageFormat m_Format;
    };

} // namespace Desert::Assets