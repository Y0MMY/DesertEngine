#pragma once

#include "IComponentWidget.hpp"

namespace Desert::Editor
{
    class AnimationComponentWidget final : public ComponentWidget<ECS::AnimationComponent>
    {
    public:
        AnimationComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager );

        bool CanRemove() const override
        {
            return false;
        }

        void Render( ECS::Entity& entity ) override;

    private:
        const std::weak_ptr<Assets::AssetManager> m_AssetManager;
    };
} // namespace Desert::Editor