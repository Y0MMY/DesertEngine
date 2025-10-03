#pragma once

#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp> //TODO: maybe fix?

namespace Desert::Runtime
{
    class SkyboxService
    {
    public:
        Common::BoolResultStr Register( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAsset );
        std::shared_ptr<Graphic::MaterialSkybox> Get( const Assets::AssetHandle& handle ) const;
        void                                     Clear();

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::MaterialSkybox>> m_Skyboxes;
    };
} // namespace Desert::Runtime