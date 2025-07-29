#pragma once

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

namespace Desert::Assets
{
    class SkyboxAsset final : public AssetBase
    {
    public:
        SkyboxAsset( AssetPriority priority, const Common::Filepath& filepath );

        Common::BoolResult Load() override;
        Common::BoolResult Unload() override;

        bool IsReadyForUse() const
        {
            return m_ReadyForUse;
        }
        const Graphic::Environment& GetEnvironment() const
        {
            return m_Environment;
        }

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Skybox;
        }

    private:
        bool                          m_ReadyForUse = false;
        std::unique_ptr<TextureAsset> m_TextureAsset;

        Graphic::Environment m_Environment;
    };
} // namespace Desert::Assets
