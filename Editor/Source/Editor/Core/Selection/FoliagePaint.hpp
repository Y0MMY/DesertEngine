#pragma once

#include <Common/Core/UUID.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace Desert::Editor::Core
{
    // Shared state for the UE5-style Foliage paint tool (active in ViewportMode::Foliage). Multiple foliage
    // TYPES can be CHECKED for painting at once (all checked types scatter under one dab); one type is the
    // "editing" selection whose detailed settings are shown. Per-type scatter params live on
    // ECS::FoliageComponent. Brush state (radius, paint density, erase) is here.
    class FoliagePaint final
    {
    public:
        // --- types checked for painting -----------------------------------------------------------------
        static std::vector<Common::UUID>& ActiveTypes() { return s_Active; }
        static bool                        HasActive() { return !s_Active.empty(); }
        static bool IsActive( const Common::UUID& u )
        {
            return std::find( s_Active.begin(), s_Active.end(), u ) != s_Active.end();
        }
        static void ToggleActive( const Common::UUID& u )
        {
            const auto it = std::find( s_Active.begin(), s_Active.end(), u );
            if ( it != s_Active.end() )
                s_Active.erase( it );
            else
                s_Active.push_back( u );
        }
        static void SetActiveOnly( const Common::UUID& u )
        {
            s_Active.clear();
            s_Active.push_back( u );
        }
        static void ClearActive() { s_Active.clear(); }

        // --- the type whose detailed settings are shown (last clicked row) ------------------------------
        static std::optional<Common::UUID> EditingType() { return s_Editing; }
        static void                        SetEditingType( const Common::UUID& u ) { s_Editing = u; }
        static void                        ClearEditingType() { s_Editing.reset(); }

        // --- brush --------------------------------------------------------------------------------------
        static float& BrushRadius() { return s_Radius; }
        static float& PaintDensity() { return s_PaintDensity; } // 0..1 multiplier on each type's Density
        static bool&  Erase() { return s_Erase; }

    private:
        static inline std::vector<Common::UUID>   s_Active;
        static inline std::optional<Common::UUID> s_Editing;
        static inline float                       s_Radius       = 300.0f; // world units (cm) = 3 m
        static inline float                       s_PaintDensity = 1.0f;
        static inline bool                        s_Erase        = false;
    };
} // namespace Desert::Editor::Core
