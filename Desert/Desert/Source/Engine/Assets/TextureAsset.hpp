#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Graphic/Texture.hpp>

#include <Engine/Assets/AssetManager.hpp>

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

        explicit TextureAsset( AssetPriority priority, const Common::Filepath& filepath, Type type );

        virtual Common::BoolResult Load() override;

        virtual Common::BoolResult Unload() override;

        virtual bool IsReadyForUse() const
        {
            return m_IsReadyForUse;
        }

        const std::shared_ptr<Graphic::Texture2D>& GetTexture() const
        {
            return m_Texture;
        }

        Type GetType() const
        {
            return m_Type;
        }

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Texture2D;
        }

    private:
        Type                                m_Type;
        bool                                m_IsReadyForUse = false;
        std::shared_ptr<Graphic::Texture2D> m_Texture;
    };

} // namespace Desert::Assets