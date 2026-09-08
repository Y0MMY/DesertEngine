#pragma once

#include "IComponentWidget.hpp"

namespace Desert::Editor
{
    class TransformComponentWidget final : public IComponentWidget
    {
    public:
        TransformComponentWidget();

        bool CanRemove() const override
        {
            return false;
        }

        void Render( ECS::Entity& entity, ::Desert::Core::Scene* scene = nullptr ) override;
    };
} // namespace Desert::Editor