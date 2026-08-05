#pragma once

#include <Engine/Reflection/ReflectionTypes.hpp>

#include <string>
#include <vector>

namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Editor::UI
{
    class UIHelper;
}

namespace Desert::Editor
{
    // Builds an ImGui property panel automatically from reflection metadata (ReflectionRegistry).
    // Works for ANY REFLECT()-annotated struct — no per-type editor code. This is the core of the
    // UE5-style Details panel: PropertyEditorBuilder::Draw(&object, typeInfo).
    //
    // assetMgr is optional; when provided, Asset<> fields (texture slots) get a drag-drop target and
    // resolve handles to asset names.
    //
    // `filter` (the Details search box) is optional; when non-empty only fields whose display name,
    // C++ name or category contain it (case-insensitively) are drawn, and empty categories disappear.
    class PropertyEditorBuilder
    {
    public:
        // Draws every visible field of `type` for the object at `object`, grouped by Category.
        // Returns true if any field value changed this frame.
        // `uiHelper` is optional; when provided, texture slots show a thumbnail tooltip on hover.
        static bool Draw( void* object, const Reflection::TypeInfo& type,
                          const Assets::AssetManager* assetMgr = nullptr, UI::UIHelper* uiHelper = nullptr,
                          const char* filter = nullptr );

        // Convenience: looks the type up in the registry by name. Returns false if not registered.
        static bool Draw( void* object, const std::string& typeName,
                          const Assets::AssetManager* assetMgr = nullptr, UI::UIHelper* uiHelper = nullptr,
                          const char* filter = nullptr );

        // Multi-select edit: draws @p primary as usual, marks fields whose value differs across
        // @p others as "(mixed)", and broadcasts a POD field edit to every object in @p others so one
        // tweak applies to the whole selection. With an empty @p others this behaves like Draw().
        static bool DrawMulti( void* primary, const std::vector<void*>& others, const Reflection::TypeInfo& type,
                               const Assets::AssetManager* assetMgr = nullptr, UI::UIHelper* uiHelper = nullptr,
                               const char* filter = nullptr );
        static bool DrawMulti( void* primary, const std::vector<void*>& others, const std::string& typeName,
                               const Assets::AssetManager* assetMgr = nullptr, UI::UIHelper* uiHelper = nullptr,
                               const char* filter = nullptr );

        // Does any visible field of `type` match the search box? Lets the panel drop a whole component
        // from the list instead of drawing an empty header.
        static bool MatchesFilter( const Reflection::TypeInfo& type, const char* filter );

        // One line describing the object's state, built from its PROPERTY(Summary) fields ("Point · 1000
        // cm"). Empty when the type declares none — the header then just shows the component name.
        static std::string BuildSummary( const void* object, const Reflection::TypeInfo& type );

        // Draws ONE field's row, outside its owning type's category grouping. Used by the pinned
        // ("Favourites") section, which hoists fields from several components to the top of Details.
        static bool DrawPinnedRow( void* object, const Reflection::TypeInfo& type,
                                   const Reflection::FieldInfo& field, const Assets::AssetManager* assetMgr,
                                   UI::UIHelper* uiHelper );

        // Stable identity of a reflected field for preferences ("TypeName.FieldName").
        static std::string FieldKey( const Reflection::TypeInfo& type, const Reflection::FieldInfo& field );

    private:
        // `defaultObject` (optional) points at a default-constructed instance of the OWNING type so a per-field
        // "reset to default" affordance can appear when a value differs from its default. `mixed` draws a
        // marker beside the label when the value differs across a multi-selection. `ownerType`, when given,
        // adds the pin (favourite) toggle — pinning needs a stable field identity.
        static bool DrawField( void* object, const Reflection::FieldInfo& field,
                               const Assets::AssetManager* assetMgr, UI::UIHelper* uiHelper,
                               const void* defaultObject = nullptr, bool mixed = false,
                               const Reflection::TypeInfo* ownerType = nullptr );
    };
} // namespace Desert::Editor
