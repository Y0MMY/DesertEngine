#pragma once

#include "IComponentWidget.hpp"

namespace Desert::Editor
{
    class PointLightComponentWidget final : public ComponentWidget<ECS::PointLightComponent>
    {
    public:
        PointLightComponentWidget();

        bool CanRemove() const override
        {
            return true;
        }

        void Render( ECS::Entity& entity ) override;

    private:
    };
} // namespace Desert::Editor