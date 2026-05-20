#pragma once

#include "IComponentWidget.hpp"

namespace Desert::Editor
{
    class PrefabComponentWidget final : public ComponentWidget<ECS::TransformComponent>
    {
    public:
        explicit PrefabComponentWidget( const Assets::AssetManager* assetManager );

        bool CanRemove() const override
        {
            return false;
        }

        void Render( ECS::Entity& entity ) override;

    private:
        const Assets::AssetManager* m_AssetManager
    };
} // namespace Desert::Editor