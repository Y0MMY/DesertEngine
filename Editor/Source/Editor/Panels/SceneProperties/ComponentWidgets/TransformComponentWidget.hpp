#pragma once

#include "IComponentWidget.hpp"

namespace Desert::Editor
{
    class TransformComponentWidget final : public ComponentWidget<ECS::TransformComponent>
    {
    public:
        TransformComponentWidget();

        bool CanRemove() const override
        {
            return false;
        }

        void Render( ECS::Entity& entity ) override;
    };
} // namespace Desert::Editor