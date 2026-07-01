#pragma once

namespace Desert::Editor::Core
{
    // UE5-style viewport tool mode (the dropdown at the viewport's top-left). Select = normal gizmo/picking
    // (skeleton-edit is a sub-mode of Select); Foliage = paint instanced vegetation with a brush.
    enum class EditorMode
    {
        Select = 0,
        Foliage
    };

    class ViewportMode final
    {
    public:
        static EditorMode Get() { return s_Mode; }
        static void       Set( EditorMode mode ) { s_Mode = mode; }
        static bool       IsFoliage() { return s_Mode == EditorMode::Foliage; }

    private:
        static inline EditorMode s_Mode = EditorMode::Select;
    };
} // namespace Desert::Editor::Core
