#pragma once

#include "IComponentWidget.hpp"

namespace Desert::Editor
{
    class CameraComponentWidget final : public ComponentWidget<ECS::CameraComponent>
    {
    public:
        CameraComponentWidget();

        void Render( ECS::Entity& entity, ::Desert::Core::Scene* scene = nullptr ) override;
    };
} // namespace Desert::Editor
