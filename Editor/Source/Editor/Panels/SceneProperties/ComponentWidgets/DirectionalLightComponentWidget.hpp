#pragma once

#include "IComponentWidget.hpp"

namespace Desert::Editor
{
    class DirectionalLightComponentWidget final : public ComponentWidget<ECS::DirectionLightComponent>
    {
    public:
        DirectionalLightComponentWidget();

        void Render( ECS::Entity& entity, ::Desert::Core::Scene* scene = nullptr ) override;
    };
} // namespace Desert::Editor
