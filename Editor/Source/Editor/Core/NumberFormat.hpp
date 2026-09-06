#pragma once

#include <cstdint>
#include <string>

namespace Desert::Editor
{
    // 1234567 -> "1 234 567".
    //
    // Every large count the editor SHOWS a person goes through here. A raw run of digits is not read, it
    // is estimated, and "148902" and "1489020" look the same at a glance. One function rather than one
    // per panel, because the Details panel's Mesh section and the status bar print the SAME triangle
    // count and two formatters would eventually group it two ways.
    //
    // THE DEFECT THIS REPLACES, which shipped and was on screen in the Mesh section: the previous form
    // computed the position of the first separator as `lead = size % 3` and then tested
    // `( i - lead ) % 3 == 0`. Both operands are size_t, so for any i < lead the subtraction wrapped to
    // near 2^64 — and 2^64-1 is divisible by 3, so the test fired and a space landed inside the leading
    // group. "12" came out as "1 2", "999" as "99 9", "13348" as "1 3 348". It was correct exactly when
    // the digit count was a multiple of three, i.e. for one magnitude in three.
    //
    // Counting DOWN cannot wrap: a separator goes before digit i whenever the number of digits still to
    // come is a multiple of three. std-only and header-only on purpose, so the rule is testable without
    // ImGui (Desert/Tests/Editor/NumberFormat).
    [[nodiscard]] inline std::string FormatThousands( uint64_t value )
    {
        const std::string digits = std::to_string( value );
        std::string       out;
        out.reserve( digits.size() + digits.size() / 3 );
        for ( std::size_t i = 0; i < digits.size(); ++i )
        {
            if ( i > 0 && ( digits.size() - i ) % 3 == 0 )
                out += ' ';
            out += digits[i];
        }
        return out;
    }
} // namespace Desert::Editor
