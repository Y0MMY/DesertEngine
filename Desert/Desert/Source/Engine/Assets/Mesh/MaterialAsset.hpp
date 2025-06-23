#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include <Engine/Graphic/Texture.hpp>

#include <Engine/Graphic/Mesh.hpp>

namespace Desert::Assets
{
    class MaterialAsset final : public AssetBase
    {
    public:
        using AssetBase::AssetBase;
        using MaterialInfo = std::optional<std::pair<Common::Filepath, std::shared_ptr<Graphic::Texture2D>>>;

        enum class MaterialTextureName : std::uint8_t
        {
            Albedo    = 0,
            Metallic  = 1,
            Roughness = 2,
        };

        virtual Common::BoolResult Load() override;
        virtual Common::BoolResult Unload() override;

        virtual bool IsReadyForUse() const
        {
            return m_ReadyForUse;
        }

        const auto GetMaterial( MaterialTextureName texture ) const
        {
            return m_MaterialsTexture[static_cast<std::uint8_t>( texture )];
        }

        bool HasMaterial( MaterialTextureName texture ) const
        {
            return m_MaterialsTexture[static_cast<std::uint8_t>( texture )].has_value();
        }

    private:
        static const std::uint8_t                     m_MeshTexturesCount = 3U;
        std::array<MaterialInfo, m_MeshTexturesCount> m_MaterialsTexture;
        bool m_ReadyForUse = false;
    };

} // namespace Desert::Assets