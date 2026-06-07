#pragma once

#include "IComponentWidget.hpp"

namespace Desert::Editor
{
    class AnimationComponentWidget final : public ComponentWidget<ECS::AnimationComponent>
    {
    public:
        AnimationComponentWidget( const Assets::AssetManager*        assetManager,
                                  const Animation::AnimationLibrary* animationLibrary );

        bool CanRemove() const override
        {
            return false;
        }

        void Render( ECS::Entity& entity, ::Desert::Core::Scene* scene = nullptr ) override;

    private:
        const Assets::AssetManager*        m_AssetManager;
        const Animation::AnimationLibrary* m_AnimationLibrary;
    };
} // namespace Desert::Editor