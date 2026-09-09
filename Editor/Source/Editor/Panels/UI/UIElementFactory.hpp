#pragma once

#include <Editor/Core/Commands/SceneCommands.hpp>
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
//
// AND IT IS ALSO WHERE THE CREATION IS RECORDED. Every other creator in the editor — the Add menu, the
// prefab drop, the viewport mesh drop, the file explorer — calls Commands::NotifyCreated so Ctrl+Z can
// take the entity back; the two UI menus were the only ones that did not, so a UI element, once added,
// could only be removed by finding it in the outliner and deleting it by hand. Putting the call inside
// the factory rather than beside each menu item is the point: a third UI menu cannot be written without
// it, which is exactly how the two menus drifted apart the first time.
namespace Desert::Editor
{
    // FindUICanvas USED TO LIVE HERE, and it was the third copy of `*reg.view<UICanvasComponent>().begin()`
    // — "the scene's canvas (the first one, which is the one the renderer draws)". Its own comment recorded
    // the coincidence as a rule. It is gone: ask ::Desert::UI::CanvasOf for the canvas an element belongs to,
    // or ::Desert::UI::SoleCanvas when there is genuinely nothing else to go on, and get a named refusal
    // instead of a winner when the scene has more than one.

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

    // Records an entity this file just created as one undo step, and hands the handle back so the caller
    // can select it. Null in, null out — a creator that failed has nothing to record.
    inline entt::entity RecordUICreation( ::Desert::Core::Scene& scene, entt::entity handle )
    {
        if ( handle == entt::null )
            return handle;

        auto& reg = scene.GetRegistry();
        if ( reg.has<ECS::UUIDComponent>( handle ) )
            Commands::NotifyCreated( { reg.get<ECS::UUIDComponent>( handle ).UUID } );
        return handle;
    }

    // Create the canvas a UI tree hangs from. It is its own function rather than three lines in the
    // viewport toolbar because it is a creation like any other and has to be recorded like one — the
    // toolbar's copy was not, so the very first thing a user does when authoring UI was the one step
    // Ctrl+Z could not take back.
    inline entt::entity CreateUICanvas( ::Desert::Core::Scene& scene )
    {
        auto& e = scene.CreateNewEntity( "UI Canvas" );
        e.AddComponent<ECS::UICanvasComponent>();
        return RecordUICreation( scene, e.GetHandle() );
    }

    // Create catalog entry @p index under @p parent. The switch is generated from the same macro as
    // kUIElements, so an entry can never be listed in a menu without a creator behind it.
    inline entt::entity CreateUIElement( ::Desert::Core::Scene& scene, entt::entity parent, std::size_t index )
    {
        std::size_t  i      = 0;
        entt::entity result = entt::null;

#define DESERT_UI_ELEMENT_CREATE( Type, EntityName, Icon, Label )                                                 \
    if ( index == i++ )                                                                                           \
        return RecordUICreation( scene, AddUIChild<ECS::Type>( scene, parent, EntityName ) );

        DESERT_UI_ELEMENT_LIST( DESERT_UI_ELEMENT_CREATE )

#undef DESERT_UI_ELEMENT_CREATE

        return result;
    }
} // namespace Desert::Editor
