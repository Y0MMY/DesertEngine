#pragma once

#include "IComponentWidget.hpp"

#include <Engine/Assets/Mesh/AnimationAsset.hpp>

#include <vector>

namespace Desert::Editor
{
    class AnimationComponentWidget final : public ComponentWidget<ECS::AnimationComponent>
    {
    public:
        // No AssetManager. It used to be a constructor parameter and a `m_AssetManager` member, and the
        // constructor never assigned one to the other — so the member was an UNINITIALISED pointer for as
        // long as the class has existed, and `-Wunused-private-field` is what finally said so. Nothing
        // here reads an AssetManager; the clip list arrives as an argument to RenderAnimGraph.
        explicit AnimationComponentWidget( const Animation::AnimationLibrary* animationLibrary );

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
        const Animation::AnimationLibrary* m_AnimationLibrary;
    };
} // namespace Desert::Editor