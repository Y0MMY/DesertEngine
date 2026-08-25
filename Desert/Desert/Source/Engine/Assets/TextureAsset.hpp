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

        // ONE identity, not two. This used to be a separate `m_Handle` member that Load assigned alongside
        // m_Metadata.Handle, with a comment explaining that the two must be kept in lock-step or every
        // handle-based resolve would miss. Two fields that must agree, and nothing checking that they do,
        // is the defect shape this whole change is about — so there is now one field.
        const auto& GetHandle() const
        {
            return m_Metadata.Handle;
        }

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Texture2D;
        }

    private:
        Type         m_Type;
        bool         m_IsReadyForUse = false;
        std::string  m_SourcePath;
        std::string  m_CookedPath;

        uint32_t m_Width;
        uint32_t m_Height;
        uint32_t m_Channels;

        Desert::Core::Formats::ImageFormat m_Format;
    };

} // namespace Desert::Assets