#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>

namespace Common::Units
{
    //  DESERT UNIT CONVENTION — one world unit is one CENTIMETRE.
    //  ---------------------------------------------------------
    //  Same convention as Unreal: 100 units = 1 m, gravity is -981 units/s², a default blockout cube is
    //  100 units, a character capsule is ~180 units tall. It holds EVERYWHERE — components, meshes, scene
    //  files, physics, shaders and the editor UI all speak centimetres, so nothing ever converts and there
    //  is no "which unit is this number in?" question.
    //
    //  Write sizes with the helpers so the intent is readable in code:
    //      float far   = Units::Metres( 1000.0f );  // 100000 units
    //      float step  = Units::Cm( 25.0f );        // 25 units
    //
    //  Scenes saved before the switch were authored in metres; SceneSerializer migrates them on load
    //  (see kUnitVersion there).

    inline constexpr float UnitsPerCm    = 1.0f;
    inline constexpr float UnitsPerMetre = 100.0f;

    inline constexpr float Cm( float cm )
    {
        return cm * UnitsPerCm;
    }
    inline constexpr float Metres( float m )
    {
        return m * UnitsPerMetre;
    }
    inline constexpr float ToMetres( float units )
    {
        return units / UnitsPerMetre;
    }

    // Read-only label for a length: centimetres below a metre ("25 cm"), metres above ("4.5 m") so a big
    // blockout doesn't turn into a wall of digits. Editable fields just show raw units with a "cm" suffix.
    inline void FormatLength( char* buf, std::size_t n, float units )
    {
        if ( std::fabs( units ) < UnitsPerMetre )
            std::snprintf( buf, n, "%.4g cm", static_cast<double>( units ) );
        else
            std::snprintf( buf, n, "%.4g m", static_cast<double>( ToMetres( units ) ) );
    }
} // namespace Common::Units
