#pragma once

#include "IComponentWidget.hpp"

namespace Desert::Editor
{
    class SkyboxComponentWidget final : public ComponentWidget<ECS::SkyboxComponent>
    {
    public:
        SkyboxComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager );

        bool CanRemove() const override
        {
            return false;
        }

        void Render( ECS::Entity& entity ) override;

    private:
        const std::weak_ptr<Assets::AssetManager> m_AssetManager;
    };
} // namespace Desert::Editor