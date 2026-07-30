#pragma once

#include "IComponentWidget.hpp"

#include <Engine/Assets/Mesh/AnimationAsset.hpp>

#include <vector>

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
        // AnimGraph (Phase 4) authoring UI: parameters (with live value controls), states (name/clip/loop/speed
        // + entry), and per-state transitions (target + blend + exit-time + parameter conditions).
        void RenderAnimGraph( ECS::AnimationComponent&                                  animation,
                              const std::vector<Assets::Asset<Assets::AnimationAsset>>& clips );

    private:
        const Assets::AssetManager*        m_AssetManager;
        const Animation::AnimationLibrary* m_AnimationLibrary;
    };
} // namespace Desert::Editor