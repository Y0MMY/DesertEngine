#pragma once

#include <Engine/Desert.hpp>
#include <ImGui/imgui_internal.h>

#include "ComponentWidgets/IComponentWidget.hpp"

namespace Desert::Editor
{
    class ComponentEditor
    {
    public:
        using ComponentFactory = std::function<std::unique_ptr<IComponentWidget>()>;

        ComponentEditor( const std::shared_ptr<Assets::AssetManager>& assetManager );
        void RegisterDefaultComponents();
        void RegisterComponent( ComponentFactory factory );
        void Render( ECS::Entity& entity );

    private:
        void RenderAddComponentPopup( ECS::Entity& entity );
        void RenderComponentHeader( IComponentWidget& widget, ECS::Entity& entity );

    private:
        std::weak_ptr<Assets::AssetManager> m_AssetManager;

        std::vector<ComponentFactory> m_AvailableComponents;
        ImGuiTextFilter               m_ComponentFilter;
    };

} // namespace Desert::Editor