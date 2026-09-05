#pragma once

#include <Editor/Panels/UI/UIElementCatalog.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/Entity.hpp>

#include <entt/entt.hpp>

#include <cstddef>
#include <string>

// The one implementation of "make a UI element". There used to be two — UIEditorPanel.cpp and
// ViewportPanel.cpp each carried a private AddUIChild template — and the two menus that called them had
// already drifted apart. Both call this now, so an element created in the panel and the same element
// created in the viewport are the same entity by construction rather than by careful copying.
namespace Desert::Editor
{
    // The scene's canvas (the first one, which is the one the renderer draws), or entt::null.
    inline entt::entity FindUICanvas( entt::registry& reg )
    {
        auto view = reg.view<ECS::UICanvasComponent>();
        return view.begin() == view.end() ? entt::null : *view.begin();
    }

    // Create a UI child entity (a UILayout plus @p ElementComponent) parented to @p parent, and return its
    // handle so the caller can select it. The UILayout is not optional: it is the rect the renderer resolves
    // anchors into, and an element without one is laid out as its parent and cannot be picked or dragged.
    template <typename ElementComponent>
    entt::entity AddUIChild( ::Desert::Core::Scene& scene, entt::entity parent, const char* name )
    {
        auto& e      = scene.CreateNewEntity( std::string( name ) );
        auto  handle = e.GetHandle();
        e.AddComponent<ECS::UILayoutComponent>();
        e.AddComponent<ElementComponent>();

        auto& reg = scene.GetRegistry();
        if ( !reg.has<ECS::RelationshipComponent>( handle ) )
            reg.emplace<ECS::RelationshipComponent>( handle );
        reg.get<ECS::RelationshipComponent>( handle ).Parent = parent;
        if ( !reg.has<ECS::RelationshipComponent>( parent ) )
            reg.emplace<ECS::RelationshipComponent>( parent );
        reg.get<ECS::RelationshipComponent>( parent ).Children.push_back( handle );
        return handle;
    }

    // Create catalog entry @p index under @p parent. The switch is generated from the same macro as
    // kUIElements, so an entry can never be listed in a menu without a creator behind it.
    inline entt::entity CreateUIElement( ::Desert::Core::Scene& scene, entt::entity parent, std::size_t index )
    {
        std::size_t  i      = 0;
        entt::entity result = entt::null;

#define DESERT_UI_ELEMENT_CREATE( Type, EntityName, Icon, Label )                                                 \
    if ( index == i++ )                                                                                           \
        return AddUIChild<ECS::Type>( scene, parent, EntityName );

        DESERT_UI_ELEMENT_LIST( DESERT_UI_ELEMENT_CREATE )

#undef DESERT_UI_ELEMENT_CREATE

        return result;
    }
} // namespace Desert::Editor
