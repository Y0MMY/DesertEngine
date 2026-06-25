#pragma once

#include <Engine/ECS/Entity.hpp>
#include <Editor/Panels/PropertyEditor/PropertyEditorBuilder.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Desert::Core
{
    class Scene;
}
namespace Desert::Assets
{
    class AssetManager;
}
namespace Desert::Animation
{
    class AnimationLibrary;
}
namespace Desert::Editor::UI
{
    class UIHelper;
}

namespace Desert::Editor
{
    // Runtime services handed to a component's Draw callback (built once per frame by ComponentEditor).
    struct ComponentEditContext
    {
        std::weak_ptr<Assets::AssetManager> AssetManager;
        const Animation::AnimationLibrary*  AnimationLibrary = nullptr;
        UI::UIHelper*                       UIHelper         = nullptr;

        Assets::AssetManager* AssetMgr() const
        {
            return AssetManager.lock().get();
        }
    };

    // One registered component editor. Built by the helpers/macros below and consumed by ComponentEditor.
    struct ComponentEditorEntry
    {
        std::string Name;
        bool        CanRemove = true;

        std::function<bool( ECS::Entity& )> Has;
        std::function<void( ECS::Entity& )> Add;
        std::function<void( ECS::Entity& )> Remove;
        std::function<void( ECS::Entity&, ::Desert::Core::Scene*, const ComponentEditContext& )> Draw;
    };

    // Editor-side registry of component editors. Components self-register at static-init via the macros
    // below, so adding one never touches ComponentEditor / the Details panel.
    class ComponentWidgetRegistry
    {
    public:
        static ComponentWidgetRegistry& Get();

        // Returns a dummy int so the call can seed a static variable (self-registration).
        int                                      Register( ComponentEditorEntry entry );
        const std::vector<ComponentEditorEntry>& Entries() const
        {
            return m_Entries;
        }

    private:
        std::vector<ComponentEditorEntry> m_Entries;
    };

    // Reflected component: ZERO UI code — the panel is auto-built from the data block's REFLECT() metadata.
    template <class ComponentT, class DataT>
    ComponentEditorEntry MakeReflectedComponentEntry( std::string name, std::string dataTypeName,
                                                      DataT ComponentT::*member, bool canRemove = true )
    {
        ComponentEditorEntry e;
        e.Name      = std::move( name );
        e.CanRemove = canRemove;
        e.Has       = []( ECS::Entity& en ) { return en.HasComponent<ComponentT>(); };
        e.Add       = []( ECS::Entity& en ) { en.AddComponent<ComponentT>(); };
        e.Remove    = []( ECS::Entity& ) {}; // preserve the current no-op remove behavior
        e.Draw      = [member, dataTypeName]( ECS::Entity& en, ::Desert::Core::Scene*,
                                         const ComponentEditContext& ctx )
        {
            auto& comp = en.GetComponent<ComponentT>();
            PropertyEditorBuilder::Draw( &( comp.*member ), dataTypeName, ctx.AssetMgr(), ctx.UIHelper );
        };
        return e;
    }

    // Custom component: bespoke UI (asset pickers, vector controls, ...). The draw lambda gets the context.
    template <class ComponentT>
    ComponentEditorEntry MakeCustomComponentEntry(
         std::string                                                                                 name,
         std::function<void( ECS::Entity&, ::Desert::Core::Scene*, const ComponentEditContext& )> draw,
         bool                                                                                        canRemove = true )
    {
        ComponentEditorEntry e;
        e.Name      = std::move( name );
        e.CanRemove = canRemove;
        e.Has       = []( ECS::Entity& en ) { return en.HasComponent<ComponentT>(); };
        e.Add       = []( ECS::Entity& en ) { en.AddComponent<ComponentT>(); };
        e.Remove    = []( ECS::Entity& ) {};
        e.Draw      = std::move( draw );
        return e;
    }
} // namespace Desert::Editor

#define DESERT_COMPONENT_CONCAT_( a, b ) a##b
#define DESERT_COMPONENT_CONCAT( a, b ) DESERT_COMPONENT_CONCAT_( a, b )

// Reflected component: one line, no widget class. Auto UI from `DataTypeName`'s reflection metadata.
//   DESERT_REGISTER_REFLECTED_COMPONENT( ECS::FooComponent, Data, "FooData", "Foo" )
#define DESERT_REGISTER_REFLECTED_COMPONENT( ComponentT, Member, DataTypeName, DisplayName )                  \
    namespace                                                                                                \
    {                                                                                                        \
        const int DESERT_COMPONENT_CONCAT( _desert_component_reg_, __COUNTER__ ) =                           \
             ::Desert::Editor::ComponentWidgetRegistry::Get().Register(                                      \
                  ::Desert::Editor::MakeReflectedComponentEntry<ComponentT>( DisplayName, DataTypeName,      \
                                                                             &ComponentT::Member ) );        \
    }

// Custom component: provide a draw lambda ( ECS::Entity&, Core::Scene*, const ComponentEditContext& ).
// Wrap the lambda in parentheses so its commas don't split the macro arguments.
#define DESERT_REGISTER_CUSTOM_COMPONENT( ComponentT, DisplayName, CanRemove, DrawLambda )                    \
    namespace                                                                                                \
    {                                                                                                        \
        const int DESERT_COMPONENT_CONCAT( _desert_component_reg_, __COUNTER__ ) =                           \
             ::Desert::Editor::ComponentWidgetRegistry::Get().Register(                                      \
                  ::Desert::Editor::MakeCustomComponentEntry<ComponentT>( DisplayName, DrawLambda,           \
                                                                          CanRemove ) );                     \
    }
