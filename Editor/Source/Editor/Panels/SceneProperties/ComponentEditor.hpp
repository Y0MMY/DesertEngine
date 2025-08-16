#pragma once

#include <Engine/Desert.hpp>
#include <ImGui/imgui_internal.h>

namespace Desert::Editor
{
    class ComponentEditor final
    {
    public:
        explicit ComponentEditor( const std::shared_ptr<Assets::AssetManager>& assetManager );
        void Render( ECS::Entity& entity );

    private:
        void RenderTransformComponent( ECS::Entity& entity );
        void RenderStaticMeshComponent( ECS::Entity& entity );

    private:
        ImGuiTextFilter m_ComponentFilter;

        const std::weak_ptr<Assets::AssetManager> m_AssetManager;
    };
} // namespace Desert::Editor