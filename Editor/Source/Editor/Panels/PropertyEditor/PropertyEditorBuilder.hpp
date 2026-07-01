#pragma once

#include <Engine/Reflection/ReflectionTypes.hpp>

#include <string>

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
    class PropertyEditorBuilder
    {
    public:
        // Draws every visible field of `type` for the object at `object`, grouped by Category.
        // Returns true if any field value changed this frame.
        // `uiHelper` is optional; when provided, texture slots show a thumbnail tooltip on hover.
        static bool Draw( void* object, const Reflection::TypeInfo& type,
                          const Assets::AssetManager* assetMgr = nullptr, UI::UIHelper* uiHelper = nullptr );

        // Convenience: looks the type up in the registry by name. Returns false if not registered.
        static bool Draw( void* object, const std::string& typeName,
                          const Assets::AssetManager* assetMgr = nullptr, UI::UIHelper* uiHelper = nullptr );

    private:
        // `defaultObject` (optional) points at a default-constructed instance of the OWNING type so a per-field
        // "reset to default" affordance can appear when a value differs from its default.
        static bool DrawField( void* object, const Reflection::FieldInfo& field,
                               const Assets::AssetManager* assetMgr, UI::UIHelper* uiHelper,
                               const void* defaultObject = nullptr );
    };
} // namespace Desert::Editor
