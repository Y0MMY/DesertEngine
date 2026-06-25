#pragma once

#include <Engine/Desert.hpp>
#include <ImGui/imgui_internal.h>

#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>

namespace Desert::Editor
{
    // Renders the Details panel for an entity by iterating the ComponentWidgetRegistry — components
    // self-register at static-init (see ComponentWidgetRegistry.hpp), so adding one never touches this
    // class. Reflected components get an auto-built UI; asset-bearing ones supply a custom draw lambda.
    class ComponentEditor
    {
    public:
        ComponentEditor( const std::shared_ptr<Assets::AssetManager>& assetManager,
                         const Animation::AnimationLibrary*           animationLibrary );

        void Render( ECS::Entity& entity, ::Desert::Core::Scene* scene = nullptr );

    private:
        ComponentEditContext MakeContext() const;
        void                 RenderAddComponentPopup( ECS::Entity& entity );
        void RenderComponentHeader( const ComponentEditorEntry& entry, ECS::Entity& entity,
                                    ::Desert::Core::Scene* scene, const ComponentEditContext& ctx );

    private:
        std::weak_ptr<Assets::AssetManager> m_AssetManager;
        const Animation::AnimationLibrary*  m_AnimationLibrary = nullptr;
        ImGuiTextFilter                     m_ComponentFilter;
    };

} // namespace Desert::Editor
